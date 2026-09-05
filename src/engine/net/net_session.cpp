#define VKM_LOG_CATEGORY "NET"

#include "net/net_session.h"

#include <algorithm>
#include <cstdio>

#include <glm/gtc/constants.hpp>

#include "logger.h"

#include "core/math/random.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"
#include "net/prediction/rewind.h"
#include "net/wire/schema.h"
#include "net/replication/spawn.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Room a snapshot body may take: the datagram ceiling, less the connection's
 * header and the snapshot's own.
 *
 * One packet per peer per frame, so a snapshot that would not fit defers entities
 * rather than becoming a second datagram.
 */
constexpr size_t MAX_PAYLOAD = UdpSocket::MAX_DATAGRAM - NetConnection::HEADER_BYTES;

/**
 * @brief Predictions kept, in ticks.
 *
 * Two hundred at 128 Hz is a second and a half, which covers any round trip a game is
 * playable on; past that the answer is too old to be worth comparing against.
 */
constexpr size_t PREDICTION_HISTORY = 200;

/**
 * @brief How long a lease outlives the last tick it was reported on.
 *
 * It has to outlive a bounce and must not outlive the push; in ticks, so it follows a
 * project's tick rate.
 */
constexpr uint32_t LEASE_TICKS = 32;

/**
 * @brief Below this a disagreement is quantisation plus float noise, so re-running ten
 * ticks of physics to close it would be paying for nothing.
 *
 * It is what keeps a replay out of the common case.
 */
constexpr float CORRECTION_FLOOR = 0.003f;

/**
 * @brief Datagrams read in one frame, per socket.
 *
 * Draining until the socket is empty makes the frame's cost the sender's
 * choice: enough traffic and the fixed step never runs, every real player times
 * out, and nothing says the cause was the network. Generous against any honest
 * load - every peer sends at most one packet per frame each way - and what is
 * left simply waits, exactly as the send side already leaves what it owes.
 */
constexpr size_t MAX_READS_PER_FRAME = 512;

/**
 * @brief How fast what is drawn catches up with what is simulated, per second.
 *
 * About a sixth of a second: slow enough not to read as a jump, fast enough that nobody
 * is shooting at where the character is not.
 */
constexpr float DRAW_CATCH_UP = 6.0f;

/**
 * @brief Past this the two ends are not describing the same thing - a teleport, a
 * respawn - and easing the picture across it would be a long visible slide to nowhere.
 *
 * Drawn where it now is, immediately.
 */
constexpr float DRAW_SNAP = 4.0f;

/**
 * @brief Returned for an entity no player drives, and by every query on a session that
 * has not started.
 *
 * Reads as nothing held.
 */
const InputCommand NO_COMMAND{};

void writeMessageHeader(BitWriter& out, NetMessage message) {
    out.u16(NET_PROTOCOL_TAG);
    out.u8(NET_PROTOCOL_VERSION);
    out.u8(static_cast<uint8_t>(message));
}

/// Read and check the four bytes every payload starts with.
bool readMessageHeader(BitReader& in, NetMessage& message) {
    if (in.u16() != NET_PROTOCOL_TAG) return false;
    if (in.u8() != NET_PROTOCOL_VERSION) return false;
    const uint8_t raw = in.u8();
    if (in.failed() || raw == 0 || raw >= static_cast<uint8_t>(NetMessage::Count)) return false;
    message = static_cast<NetMessage>(raw);
    return true;
}

} // namespace

/**
 * @brief One connected player, from the server's side.
 *
 * Everything about a player lives here and dies with them. A seat outlives the
 * player who sat in it, so state kept per seat and reused - a baseline, a
 * command queue - hands the next player deltas against a world they never held.
 * Allocating and destroying this per connection makes that impossible rather
 * than merely avoided.
 */
struct NetSession::Peer {
    NetConnection    connection;
    NetBaseline      baseline;
    NetCommandBuffer commands;
    NetReliable      reliable;
    InputCommand     current;         ///< What this player's entity runs this tick.
    PlayerId         id = NO_PLAYER;
    EntityId         entity;
    const char*      leaving = nullptr;  ///< Why this peer is going, once it is.
    uint64_t         token = 0;            ///< What proves a packet is this player's.
    bool             answered = false;     ///< Whether the token has come back.

    /**
     * @brief Which server tick this client says it is drawing.
     *
     * Its own number, in this server's clock, because that is the domain its
     * interpolation runs in - so it needs no translating and no estimate from ping.
     */
    float            renderTick = 0.0f;
};

NetSession::NetSession()  = default;
NetSession::~NetSession() = default;

bool NetSession::host(uint16_t port, uint32_t maxPlayers) {
    close();

    // A project saying it is for no players means it, and the editor offers
    // zero as the bottom of that field. Serving one seat instead would let the
    // next connection in to a game the author had closed.
    if (maxPlayers == 0) {
        m_lastError = "this project is for no players";
        LOG_ERROR("Cannot host: the project says it has no seats");
        return false;
    }
    if (!m_socket.open(port)) {
        m_lastError = "could not bind the port";
        LOG_ERROR("Cannot host on port %u: the port is in use or not permitted", port);
        return false;
    }
    m_role        = NetRole::Server;
    m_maxPlayers  = maxPlayers;
    m_localPlayer = NO_PLAYER;
    m_localEntity = EntityId{};
    m_lastError.clear();
    LOG_INFO("Hosting on port %u, %u seat(s), schema: %s",
             m_socket.localAddress().port, m_maxPlayers,
             NetSchema::get().describe().c_str());
    return true;
}

bool NetSession::connect(const NetAddress& server) {
    close();
    if (!m_socket.open(0)) {
        m_lastError = "could not open a socket";
        return false;
    }
    m_role   = NetRole::Client;
    m_server = std::make_unique<NetConnection>();
    m_server->open(server);
    m_lastError.clear();

    // Nothing is sent yet: a project defines its actions from onStart, inside
    // the first tick, so a hello from here would carry a count of zero. The
    // frame after the first tick says hello, which costs a frame and is right.
    LOG_INFO("Joining %s", server.toString().c_str());
    return true;
}

void NetSession::close() {
    if (m_role == NetRole::Offline) return;

    // Say so rather than simply stopping. A peer that is told is a peer that
    // frees the seat now instead of five seconds from now, and a player
    // watching a scoreboard sees the right thing.
    std::vector<uint8_t> payload;
    BitWriter writer(payload, 16);
    writeMessageHeader(writer, NetMessage::Goodbye);
    writer.u64(m_token);
    writer.finish();

    if (m_role == NetRole::Client && m_server) {
        m_server->frame(payload.data(), payload.size(), m_datagram);
        m_socket.send(m_server->peer(), m_datagram.data(), m_datagram.size());
    } else {
        for (auto& peer : m_peers) {
            peer->connection.frame(payload.data(), payload.size(), m_datagram);
            m_socket.send(peer->connection.peer(), m_datagram.data(), m_datagram.size());
        }
    }

    m_peers.clear();
    m_server.reset();
    m_socket.close();
    m_role        = NetRole::Offline;
    m_localPlayer = NO_PLAYER;
    m_localEntity = EntityId{};
    m_prediction.clear();
    m_reliable.clear();
    m_interpolation.clear();
    m_rewind.clear();
    m_rewound.clear();
    m_sendAccumulator = 1.0f;
    m_serverTick      = 0;
    m_warnedTickJump  = false;
    m_lastSnapshot    = NetSnapshotStats{};
}

void NetSession::onSpawn(SpawnPlayer spawn, DespawnPlayer despawn) {
    m_spawn   = std::move(spawn);
    m_despawn = std::move(despawn);
}

bool NetSession::isPlaying() const {
    if (m_role == NetRole::Server)  return true;
    if (m_role == NetRole::Offline) return false;
    return m_localPlayer != NO_PLAYER;
}

float NetSession::roundTrip() const {
    return (m_role == NetRole::Client && m_server) ? m_server->roundTrip() : 0.0f;
}

size_t NetSession::playerCount() const {
    if (m_role == NetRole::Server) return m_peers.size();
    return isPlaying() ? 1u : 0u;
}

bool NetSession::simulates(EntityId entity) const {
    if (isAuthority(m_role)) return true;
    if (!entity) return false;
    if (entity == m_localEntity) return true;

    for (const Lease& lease : m_prediction.leases) {
        if (lease.entity == entity) return true;
    }
    return false;
}

void NetSession::lease(const std::vector<EntityId>& entities) {
    for (EntityId entity : entities) {
        if (!entity || entity == m_localEntity) continue;

        const auto found = std::find_if(m_prediction.leases.begin(), m_prediction.leases.end(),
                                        [entity](const Lease& lease) {
                                            return lease.entity == entity;
                                        });
        if (found != m_prediction.leases.end()) {
            found->ticks = LEASE_TICKS;
            continue;
        }
        m_prediction.leases.push_back({entity, LEASE_TICKS});
        m_prediction.acquired.push_back(entity);

        // Held, not forgotten: this end predicts the body now, so drawing it a
        // moment behind as well would be two answers fighting over one
        // transform. The track stays, and is what it slides back to on expiry.
        m_interpolation.hold(entity, true);
    }
}

bool NetSession::isMine(EntityId entity) const {
    // Offline there is one player and everything is theirs, the same answer
    // simulates() gives, so a caller never asks isOffline() first. A server
    // holds no local entity, so its answer is no with no case saying so.
    if (m_role == NetRole::Offline) return true;
    return entity && entity == m_localEntity;
}

const InputCommand& NetSession::commandFor(EntityId entity) const {
    // The player at this end, which is a client's own character or, offline,
    // whatever is asked about. A server holds no entity of its own.
    if (entity && entity == m_localEntity) return m_currentCommand;

    if (m_role == NetRole::Server) {
        for (const auto& peer : m_peers) {
            if (peer->entity == entity) return peer->current;
        }
        return NO_COMMAND;
    }
    // Offline drives whatever it is asked about, because there is only one
    // player and no ownership to disagree with. A client drives what it owns.
    if (m_role == NetRole::Offline) return m_currentCommand;
    return NO_COMMAND;
}

void NetSession::beginTick(uint32_t tick, const InputCommand& command, uint32_t actionCount) {
    m_currentCommand = command;
    m_ticked         = true;

    // Aged here because this runs once per tick in every role and needs nothing
    // else - not the scene, not the clock. A lapsed lease hands the body back,
    // and the track held all along is what it slides to.
    if (!m_prediction.replaying) {
        for (Lease& lease : m_prediction.leases) {
            if (lease.ticks > 0) --lease.ticks;
        }
        const auto lapsed = std::remove_if(m_prediction.leases.begin(), m_prediction.leases.end(),
                                           [](const Lease& lease) { return lease.ticks == 0; });
        for (auto it = lapsed; it != m_prediction.leases.end(); ++it) {
            m_interpolation.hold(it->entity, false);
            m_prediction.released.push_back(it->entity);
        }
        m_prediction.leases.erase(lapsed, m_prediction.leases.end());
    }

    // A server's count comes from the client that joined, not from here: its
    // own scene may not have started its behaviors yet, and a zero here would
    // decode every command that client sends against it.
    if (m_role != NetRole::Server) m_actionCount = actionCount;

    if (m_role == NetRole::Client) {
        m_prediction.unacknowledged.push_back(m_currentCommand);
        // Held until the server says it ran them, not until the next packet
        // goes out: one frame can run many ticks, and a command dropped before
        // it was sent is a tick predicted here and never run there.
        if (m_prediction.unacknowledged.size() > NET_COMMAND_MEMORY) {
            m_prediction.unacknowledged.erase(m_prediction.unacknowledged.begin());
        }
    } else if (m_role == NetRole::Server) {
        for (auto& peer : m_peers) {
            peer->current = peer->commands.take(tick);
        }
    }
}

void NetSession::restoreHeld(Scene& scene) {
    for (const Held& held : m_prediction.held) {
        if (!scene.isAlive(held.entity)) continue;
        scene.get<Transform>(held.entity) = held.transform;
        if (held.hasBody && scene.has<Rigidbody>(held.entity)) {
            scene.get<Rigidbody>(held.entity) = held.body;
        }
    }
}

void NetSession::reconcile(Scene& scene, uint32_t confirmedTick) {
    // What the server said this end's character was doing, at the last tick it
    // had this end's input for. The snapshot has already written it into the
    // scene, which is exactly the state a replay has to start from.
    const glm::vec3 authoritative = scene.get<Transform>(m_localEntity).position;

    const auto found = std::find_if(m_prediction.predicted.begin(), m_prediction.predicted.end(),
                                    [confirmedTick](const Predicted& p) {
                                        return p.tick == confirmedTick;
                                    });

    // Nothing to compare against - the confirmation is older than anything
    // remembered, which happens once at the start of a session. The prediction
    // stands: believing a moment already passed is the jump this prevents.
    if (found == m_prediction.predicted.end()) {
        restoreHeld(scene);
        return;
    }

    // Confirmed commands need not be carried again, and what is left is exactly
    // the ticks a replay would run.
    m_prediction.unacknowledged.erase(
        std::remove_if(m_prediction.unacknowledged.begin(), m_prediction.unacknowledged.end(),
                       [confirmedTick](const InputCommand& command) {
                           return command.tick <= confirmedTick;
                       }),
        m_prediction.unacknowledged.end());

    m_prediction.error = glm::length(authoritative - found->position);

    if (m_prediction.error < CORRECTION_FLOOR) {
        restoreHeld(scene);
        m_prediction.predicted.erase(m_prediction.predicted.begin(), found + 1);
        return;
    }

    // A replay needs an unbroken run of commands from the tick after the
    // confirmed one; with a gap the ticks run out of order, which is a
    // different wrong answer. The prediction stands and the next one tries.
    if (m_prediction.unacknowledged.empty() || m_prediction.unacknowledged.front().tick != confirmedTick + 1) {
        // Said once. A replay needs every command since the confirmed tick, and
        // the buffer holds NET_COMMAND_MEMORY - past that the prediction is
        // never corrected again and nothing else would say why.
        if (!m_prediction.warnedNoReplay) {
            m_prediction.warnedNoReplay = true;
            LOG_WARNING("The round trip has outrun the commands kept for a replay, so this "
                        "end's prediction is no longer being corrected");
        }
        restoreHeld(scene);
        m_prediction.predicted.erase(m_prediction.predicted.begin(), found + 1);
        return;
    }

    // What the picture owes is how far the replay moves a body, so it is
    // measured in endReplay against the answer rather than here against a tick
    // a round trip old.
    m_prediction.replay         = m_prediction.unacknowledged;
    m_prediction.warnedNoReplay = false;

    // All of it, not the confirmed prefix: the replay records every tick again,
    // and two answers for one tick means the next snapshot finds the older and
    // corrects an error already corrected.
    m_prediction.predicted.clear();
}

void NetSession::beginReplayTick(const InputCommand& command) {
    m_currentCommand = command;
    m_prediction.replaying      = true;
}

void NetSession::endReplay(Scene& scene) {
    const bool ran = !m_prediction.replay.empty();
    m_prediction.replaying = false;
    m_prediction.replay.clear();
    if (!ran) return;

    // What the picture owes, for everything this end predicts: where the player
    // last saw each body, less where the replay put it. Measured here because
    // until the ticks have run the answer is not known - see networking.md.
    for (const Held& held : m_prediction.held) {
        if (!scene.isAlive(held.entity) || !scene.has<Transform>(held.entity)) continue;
        correctDraw(held.entity, held.transform.position + drawOffsetFor(held.entity),
                    scene.get<Transform>(held.entity).position);
    }
}

glm::vec3 NetSession::drawOffsetFor(EntityId entity) const {
    for (const DrawCorrection& correction : m_prediction.corrections) {
        if (correction.entity == entity) return correction.offset;
    }
    return glm::vec3(0.0f);
}

void NetSession::correctDraw(EntityId entity, const glm::vec3& drawn,
                             const glm::vec3& simulated) {
    // Set rather than added to, because what came in already counts the offset
    // still outstanding. The whole debt is restated, so the picture carries on
    // from exactly where it was and no correction is ever counted twice.
    glm::vec3 offset = drawn - simulated;

    // Past this the two ends are not describing the same thing - a teleport, a
    // respawn - and easing across it would be a long visible slide.
    if (glm::length(offset) > DRAW_SNAP) offset = glm::vec3(0.0f);

    for (DrawCorrection& correction : m_prediction.corrections) {
        if (correction.entity != entity) continue;
        correction.offset = offset;
        return;
    }
    if (glm::length(offset) < glm::epsilon<float>()) return;
    m_prediction.corrections.push_back({entity, offset});
}

NetSession::Drawn::Drawn(NetSession& session, Scene& scene, float deltaTime)
    : m_session(session), m_scene(scene) {
    m_session.applyDrawCorrection(m_scene, deltaTime);
}

NetSession::Drawn::~Drawn() {
    m_session.removeDrawCorrection(m_scene);
}

void NetSession::applyDrawCorrection(Scene& scene, float deltaTime) {
    if (m_role != NetRole::Client) return;

    // Worked off over a handful of frames rather than at once: the simulation
    // has already taken each correction exactly, and this is only the picture
    // catching up with it.
    const float kept = std::max(0.0f, 1.0f - DRAW_CATCH_UP * deltaTime);
    for (DrawCorrection& correction : m_prediction.corrections) correction.offset *= kept;

    m_prediction.corrections.erase(
        std::remove_if(m_prediction.corrections.begin(), m_prediction.corrections.end(),
                       [&scene](const DrawCorrection& correction) {
                           return glm::length(correction.offset) < glm::epsilon<float>()
                               || !scene.isAlive(correction.entity);
                       }),
        m_prediction.corrections.end());

    for (const DrawCorrection& correction : m_prediction.corrections) {
        offsetDrawn(scene, correction.entity, correction.offset);
    }
}

void NetSession::removeDrawCorrection(Scene& scene) {
    if (m_role != NetRole::Client) return;
    for (const DrawCorrection& correction : m_prediction.corrections) {
        offsetDrawn(scene, correction.entity, -correction.offset);
    }
}

void NetSession::offsetDrawn(Scene& scene, EntityId entity, const glm::vec3& by) {
    if (!entity || !scene.isAlive(entity) || !scene.has<Transform>(entity)) return;
    scene.get<Transform>(entity).position += by;
}

void NetSession::endTick(Scene& scene, uint32_t tick) {
    // Only where this end is the authority. A client learns the server's tick
    // from a snapshot header and from nowhere else.
    if (isAuthority(m_role)) m_serverTick = tick;

    if (m_role == NetRole::Server) {
        // Only players: that is what players
        // shoot at, it is bounded by the seat count, and remembering every body
        // in the world would be a ring per crate.
        const auto remember = [&](EntityId entity) {
            if (!entity || !scene.isAlive(entity) || !scene.has<Transform>(entity)) return;
            const Transform& at = scene.get<Transform>(entity);
            m_rewind.record(entity, tick, at.position, at.rotation);
        };
        for (const auto& peer : m_peers) remember(peer->entity);
        return;
    }

    if (m_role != NetRole::Client || !m_localEntity) return;
    if (!scene.isAlive(m_localEntity) || !scene.has<Transform>(m_localEntity)) return;

    m_prediction.predicted.push_back({tick, scene.get<Transform>(m_localEntity).position});
    if (m_prediction.predicted.size() > PREDICTION_HISTORY) {
        m_prediction.predicted.erase(m_prediction.predicted.begin());
    }
}

void NetSession::interpolate(Scene& scene, float deltaTime, float tickRate) {
    if (m_role != NetRole::Client) return;

    // A lease lapsing moves a body from predicted to drawn-from-the-past
    // without the body moving, so the step is measured across the call that
    // causes it and worked off like any other correction.
    std::vector<glm::vec3> before;
    before.reserve(m_prediction.released.size());
    for (EntityId entity : m_prediction.released) {
        const bool drawn = entity && scene.isAlive(entity) && scene.has<Transform>(entity);
        before.push_back(drawn ? scene.get<Transform>(entity).position + drawOffsetFor(entity)
                               : glm::vec3(0.0f));
    }

    m_interpolation.apply(scene, deltaTime, tickRate, NET_SNAPSHOT_RATE);

    for (size_t i = 0; i < m_prediction.released.size(); ++i) {
        const EntityId entity = m_prediction.released[i];
        if (!entity || !scene.isAlive(entity) || !scene.has<Transform>(entity)) continue;
        correctDraw(entity, before[i], scene.get<Transform>(entity).position);
    }
    m_prediction.released.clear();
}

float NetSession::renderDelay() const {
    if (m_role != NetRole::Client) return 0.0f;
    return m_interpolation.behindTicks();
}

void NetSession::advance(float seconds) {
    m_sendAccumulator += seconds;

    if (m_role == NetRole::Server) {
        for (auto& peer : m_peers) peer->connection.advance(seconds);
    } else if (m_server) {
        m_server->advance(seconds);
    }
}

NetSession::Peer* NetSession::findPeer(const NetAddress& address) {
    for (auto& peer : m_peers) {
        if (peer->connection.peer() == address) return peer.get();
    }
    return nullptr;
}

void NetSession::forgetWorld() {
    m_prediction.clear();
    m_interpolation.clear();
    m_rewind.clear();
    m_rewound.clear();
}

void NetSession::receive(Scene& scene, ResourceManager& resources) {
    if (m_role == NetRole::Offline) return;

    // A scene that has been replaced under a live session takes every entity
    // this end was remembering with it, and the replacement reuses the same
    // ids - so nothing cached can be told apart from what it replaced.
    if (scene.epoch() != m_worldEpoch) {
        if (m_worldEpoch != 0) {
            LOG_WARNING("The world was replaced while a session was live; "
                        "what this end remembered about it is dropped");
            forgetWorld();
        }
        m_worldEpoch = scene.epoch();
    }

    if (m_role == NetRole::Server) receiveAsServer(scene, resources);
    else                           receiveAsClient(scene, resources);
}

void NetSession::receiveAsServer(Scene& scene, ResourceManager& resources) {
    NetAddress from;
    // Bounded rather than drained - see MAX_READS_PER_FRAME.
    for (size_t read = 0; read < MAX_READS_PER_FRAME; ++read) {
        if (!m_socket.receive(m_datagram, from)) break;
        Peer* peer = findPeer(from);
        if (!peer) {
            greet(from, m_datagram.data(), m_datagram.size(), scene, resources);
            continue;
        }

        m_acknowledged.clear();
        if (!peer->connection.accept(m_datagram.data(), m_datagram.size(),
                                     m_scratch, m_acknowledged)) {
            continue;
        }
        for (uint16_t sequence : m_acknowledged) peer->baseline.confirm(sequence);

        BitReader reader(m_scratch.data(), m_scratch.size());
        NetMessage message = NetMessage::Hello;
        if (!readMessageHeader(reader, message)) continue;

        // Everything a seat's owner says after the Welcome carries the token.
        // Without it an address is the whole of a peer's identity: one forged
        // Goodbye ends a match, one forged Command steers their character.
        if (message == NetMessage::Goodbye || message == NetMessage::Command) {
            const uint64_t token = reader.u64();
            if (reader.failed() || token != peer->token) continue;

            // Echoing it is the proof this address can receive, the Welcome being
            // the only place it was said. A sequence number cannot do this job: a
            // sender writes the acknowledgement field itself.
            peer->answered = true;
        }

        if (message == NetMessage::Goodbye) {
            // Marked rather than aged past the timeout: a peer that said so is
            // not a peer that stopped answering, and the sweep below is the one
            // place a seat is given back.
            peer->leaving = "said goodbye";
            continue;
        }
        // Still asking to join, so the Welcome did not arrive. Said again
        // rather than ignored: the seat is otherwise held for the life of the
        // process against a client that can never finish joining.
        if (message == NetMessage::Hello) {
            welcome(*peer, from);
            continue;
        }
        if (message == NetMessage::Command) acceptCommands(*peer, reader);
    }

    // Dropped after the read loop rather than inside it, so a peer leaving does
    // not invalidate the list a datagram is still being matched against.
    for (size_t i = m_peers.size(); i-- > 0;) {
        if (m_peers[i]->leaving) {
            dropPeer(scene, resources, i, m_peers[i]->leaving);
        } else if (m_peers[i]->connection.timedOut()) {
            dropPeer(scene, resources, i, "stopped answering");
        }
    }
}

void NetSession::acceptCommands(Peer& peer, BitReader& reader) {
    // A client's reliable stream carries nothing yet, but the field is in the
    // packet either way and has to be stepped over to reach what follows.
    std::vector<std::vector<uint8_t>> ignored;
    if (!peer.reliable.read(reader, ignored)) return;

    const uint32_t drawnWhole    = reader.u32();
    const uint32_t drawnFraction = reader.bits(8);
    if (reader.failed()) return;
    peer.renderTick = static_cast<float>(drawnWhole)
                    + static_cast<float>(drawnFraction) / 255.0f;

    std::vector<InputCommand> commands;
    if (!readCommands(reader, m_actionCount, commands)) {
        LOG_ERROR("Player %u sent a command packet that did not decode", peer.id);
        return;
    }
    for (const InputCommand& command : commands) peer.commands.accept(command);
}

bool NetSession::onWelcome(Scene& scene, BitReader& reader) {
    m_localPlayer = static_cast<PlayerId>(reader.u16());
    const uint32_t slot = reader.u32();

    // Bounded like every other slot off the wire: createEntityAt grows its
    // table to whatever index it is handed, so one bad Welcome asks the
    // process for gigabytes.
    if (reader.failed() || slot == 0 || slot > NET_MAX_SLOT) {
        m_lastError = "the server named an entity this world cannot hold";
        LOG_ERROR("Welcome named slot %u; the wire may name up to %u", slot, NET_MAX_SLOT);
        close();
        return false;
    }
    m_token = reader.u64();
    if (reader.failed()) {
        // Treated like the slot check above rather than shrugged at: a Welcome
        // that stops here leaves this end reporting itself as playing with no
        // entity and a token the server will reject on every packet after.
        m_lastError = "the server's welcome ended before it said who this end is";
        LOG_ERROR("Welcome ended before the token; not joining");
        close();
        return false;
    }

    m_localEntity = scene.isAliveAtIndex(slot) ? scene.entityAt(slot)
                                               : scene.createEntityAt(slot);
    LOG_INFO("Joined as player %u, driving entity %u", m_localPlayer, slot);
    return true;
}

bool NetSession::onRefuse(BitReader& reader) {
    const auto reason = static_cast<NetRefusal>(reader.u8());
    m_lastError = toString(reason);
    LOG_ERROR("Refused: %s", m_lastError.c_str());
    if (reason == NetRefusal::Mismatch) {
        LOG_ERROR("This end replicates: %s", NetSchema::get().describe().c_str());
    }
    close();
    return false;
}

bool NetSession::onGoodbye() {
    m_lastError = "the server closed the game";
    LOG_INFO("The server closed the game");
    close();
    m_role = NetRole::Disconnected;
    return false;
}

void NetSession::holdPredicted(Scene& scene, std::vector<DrawnFrom>& takenFrom) {
    // Held aside because the snapshot is about to overwrite what this end
    // predicts with a moment already passed. Confirmed values go in first, so
    // "unchanged" means the server's rather than this end's.
    m_prediction.held.clear();
    const auto holdAside = [&](EntityId entity) {
        if (!entity || !scene.isAlive(entity) || !scene.has<Transform>(entity)) return;
        Held held;
        held.entity    = entity;
        held.transform = scene.get<Transform>(entity);
        held.hasBody   = scene.has<Rigidbody>(entity);
        if (held.hasBody) held.body = scene.get<Rigidbody>(entity);
        m_prediction.held.push_back(held);
    };
    holdAside(m_localEntity);

    // A body taken this snapshot is the exception: restoring an interpolated
    // pose would start this end's simulation a render delay behind and keep it
    // there. Where it was drawn is kept instead, and eased across.
    takenFrom.clear();
    for (const Lease& lease : m_prediction.leases) {
        const bool justTaken = std::find(m_prediction.acquired.begin(), m_prediction.acquired.end(),
                                         lease.entity) != m_prediction.acquired.end();
        if (!justTaken) {
            holdAside(lease.entity);
        } else if (scene.isAlive(lease.entity) && scene.has<Transform>(lease.entity)) {
            takenFrom.push_back({lease.entity,
                                 scene.get<Transform>(lease.entity).position
                                     + drawOffsetFor(lease.entity)});
        }
    }
}

bool NetSession::adoptServerTick(uint32_t serverTick) {
    // One impossible tick puts the clock that decides what is drawn where no
    // sample will reach, for the rest of the session. Refused whole.
    if (m_serverTick != 0 && serverTick > m_serverTick + NET_MAX_TICK_JUMP) {
        m_server->refuse();
        if (!m_warnedTickJump) {
            m_warnedTickJump = true;
            LOG_ERROR("A snapshot claimed tick %u against %u; ignoring it and any like it",
                      serverTick, m_serverTick);
        }
        return false;
    }
    m_serverTick     = serverTick;
    m_warnedTickJump = false;
    return true;
}

void NetSession::onSnapshot(Scene& scene, ResourceManager& resources, BitReader& reader) {
    const uint32_t serverTick    = reader.u32();
    const uint32_t confirmedTick = reader.u32();

    if (!adoptServerTick(serverTick)) return;

    std::vector<std::vector<uint8_t>> messages;
    if (!m_reliable.read(reader, messages)) {
        m_server->refuse();
        LOG_ERROR("A snapshot's message block did not decode");
        return;
    }
    for (const std::vector<uint8_t>& message : messages) applyMessage(scene, resources, message);

    std::vector<DrawnFrom> takenFrom;
    holdPredicted(scene, takenFrom);

    // holdPredicted already captured the character; this is only whether there
    // is one to reconcile against.
    const bool predicting = m_localEntity && scene.isAlive(m_localEntity)
                         && scene.has<Transform>(m_localEntity);

    // What the smoothing drew goes back to what the server said before a word
    // of the snapshot is read: an absent component means "the same as last
    // time", and last time is the server's.
    m_interpolation.restoreConfirmed(scene);

    m_touched.clear();
    if (!readSnapshot(scene, NetSchema::get(), reader, &m_touched)) {
        // Un-acknowledged as well as unread: a sender never describes what it
        // believes this end already holds, so a standing claim would freeze
        // every body in it for the match.
        m_server->refuse();
        LOG_ERROR("A snapshot did not decode; waiting for the next one");
        return;
    }

    for (uint32_t slot : m_touched) {
        if (!scene.isAliveAtIndex(slot)) continue;
        const EntityId entity = scene.entityAt(slot);
        // The entity this end drives is predicted, not drawn late.
        if (entity == m_localEntity || !scene.has<Transform>(entity)) continue;
        const Transform& at = scene.get<Transform>(entity);
        m_interpolation.record(entity, serverTick, at.position, at.rotation);
    }

    // A body just taken keeps the server's word and eases across the handover,
    // as one handed back does: the same transition in opposite directions.
    for (const DrawnFrom& taken : takenFrom) {
        if (!scene.isAlive(taken.entity) || !scene.has<Transform>(taken.entity)) continue;
        correctDraw(taken.entity, taken.drawn, scene.get<Transform>(taken.entity).position);
    }
    m_prediction.acquired.clear();

    // What this end predicted is put back, or kept and re-run from the server's
    // answer - one decision, for the character and every body it is pushing.
    if (predicting) reconcile(scene, confirmedTick);
    else            restoreHeld(scene);
}

void NetSession::receiveAsClient(Scene& scene, ResourceManager& resources) {
    NetAddress from;
    // Bounded rather than drained - see MAX_READS_PER_FRAME.
    for (size_t read = 0; read < MAX_READS_PER_FRAME; ++read) {
        if (!m_socket.receive(m_datagram, from)) break;
        if (!m_server || !(from == m_server->peer())) continue;

        m_acknowledged.clear();
        if (!m_server->accept(m_datagram.data(), m_datagram.size(), m_scratch, m_acknowledged)) {
            continue;
        }

        BitReader reader(m_scratch.data(), m_scratch.size());
        NetMessage message = NetMessage::Hello;
        if (!readMessageHeader(reader, message)) continue;

        // A handler that ended the session says so, because the rest of what is
        // waiting belongs to a session that is over.
        bool open = true;
        switch (message) {
            case NetMessage::Welcome:  open = onWelcome(scene, reader); break;
            case NetMessage::Refuse:   open = onRefuse(reader);         break;
            case NetMessage::Goodbye:  open = onGoodbye();              break;
            case NetMessage::Snapshot: onSnapshot(scene, resources, reader); break;
            default: break;
        }
        if (!open) return;
    }

    if (m_server && m_server->timedOut()) {
        m_lastError = "the server stopped answering";
        LOG_ERROR("Lost the server: nothing heard for %.0f seconds",
                  NetConnection::TIMEOUT_SECONDS);
        close();
        m_role = NetRole::Disconnected;
    }
}

bool NetSession::vetHello(const NetAddress& from, BitReader& reader, uint32_t& actions) {
    const uint32_t fingerprint = reader.u32();
    actions                    = reader.u8();
    const uint64_t world       = reader.u64();
    if (reader.failed()) return false;

    // Every refusal below is counted and only the first says why: a line per
    // refused packet is a server's disk spent from off the machine. The count
    // rides the status line once a second.
    if (fingerprint != NetSchema::get().fingerprint()) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR("Refusing a join: a different build or project. This end "
                      "replicates: %s", NetSchema::get().describe().c_str());
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch);
        return false;
    }

    // A slot is an entity's name, so two ends whose scenes differ agree on every
    // name and mean a different thing by each. Compared only when both have a
    // file behind their world; a generated one says so with zero.
    if (m_world != 0 && world != 0 && world != m_world) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR("Refusing a join: a different world. This end loaded %016llx, "
                      "the other %016llx",
                      static_cast<unsigned long long>(m_world),
                      static_cast<unsigned long long>(world));
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch);
        return false;
    }

    // Refused before it is stored, let alone used. The codec bounds it too, but
    // a peer that sent one is a broken build, and a refusal with a reason beats
    // a connection whose every packet is discarded.
    if (actions > MAX_INPUT_ACTIONS) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR("Refusing %s: it claims %u actions and a command holds %u",
                      from.toString().c_str(), actions, MAX_INPUT_ACTIONS);
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch);
        return false;
    }

    // Every player in one game must agree, because the count is the width of
    // every command field and a second player who disagreed would have their
    // input read as somebody else's actions.
    if (!m_peers.empty() && actions != m_actionCount) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR("Refusing %s: it defines %u actions and this game has %u",
                      from.toString().c_str(), actions, m_actionCount);
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch);
        return false;
    }

    if (m_peers.size() >= m_maxPlayers) {
        ++m_refusedFull;
        refuse(from, NetRefusal::Full);
        return false;
    }
    return true;
}

void NetSession::greet(const NetAddress& from, const uint8_t* payload, size_t size,
                       Scene& scene, ResourceManager& resources) {
    // One chance to be a Hello, read on a throwaway connection so anything else
    // costs nothing and is dropped in silence.
    NetConnection probe;
    probe.open(from);
    m_acknowledged.clear();
    if (!probe.accept(payload, size, m_scratch, m_acknowledged)) return;

    BitReader reader(m_scratch.data(), m_scratch.size());
    NetMessage message = NetMessage::Hello;
    if (!readMessageHeader(reader, message) || message != NetMessage::Hello) return;

    uint32_t actions = 0;
    if (!vetHello(from, reader, actions)) return;

    // The same datagram, read again on the connection that will keep it. One
    // memcpy on a join, and it is what lets a connection be non-movable like
    // every other stateful type here.
    auto peer = std::make_unique<Peer>();
    peer->connection.open(from);
    m_acknowledged.clear();
    if (!peer->connection.accept(payload, size, m_scratch, m_acknowledged)) return;

    // A source address is trivially forged, so it is not on its own an
    // identity: this is what a packet has to carry to be this player's. Sixty
    // four random bits, told to the seat's owner once in the Welcome.
    peer->token  = (static_cast<uint64_t>(Math::Random::rng().nextU32()) << 32)
                 | Math::Random::rng().nextU32();
    peer->id     = m_nextPlayer++;
    peer->entity = m_spawn ? m_spawn(scene, resources, peer->id) : EntityId{};
    if (!peer->entity) {
        LOG_INFO("Refusing %s: the game declined", from.toString().c_str());
        refuse(from, NetRefusal::Declined);
        return;
    }

    m_actionCount = actions;
    welcome(*peer, from);

    LOG_INFO("Player %u joined from %s, driving entity %u (%zu of %u seats)",
             peer->id, from.toString().c_str(), peer->entity.slot(),
             m_peers.size() + 1, m_maxPlayers);
    m_peers.push_back(std::move(peer));
}

void NetSession::welcome(Peer& peer, const NetAddress& to) {
    std::vector<uint8_t> body;
    BitWriter writer(body, 64);
    writeMessageHeader(writer, NetMessage::Welcome);
    writer.u16(peer.id);
    writer.u32(peer.entity.slot());
    writer.u64(peer.token);
    writer.finish();

    peer.connection.frame(body.data(), body.size(), m_datagram);
    m_socket.send(to, m_datagram.data(), m_datagram.size());
}

void NetSession::refuse(const NetAddress& from, NetRefusal reason) {
    NetConnection reply;
    reply.open(from);

    std::vector<uint8_t> payload;
    BitWriter writer(payload, 16);
    writeMessageHeader(writer, NetMessage::Refuse);
    writer.u8(static_cast<uint8_t>(reason));
    writer.finish();

    reply.frame(payload.data(), payload.size(), m_datagram);
    m_socket.send(from, m_datagram.data(), m_datagram.size());
}

void NetSession::dropPeer(Scene& scene, ResourceManager& resources, size_t index, const char* why) {
    Peer& peer = *m_peers[index];
    LOG_INFO("Player %u left: %s", peer.id, why);
    if (m_despawn && peer.entity) m_despawn(scene, resources, peer.id, peer.entity);
    m_rewind.forget(peer.entity);
    m_peers.erase(m_peers.begin() + static_cast<long>(index));
}

void NetSession::send(Scene& scene, uint32_t tick) {
    if (m_role == NetRole::Offline) return;

    const float interval = 1.0f / (m_role == NetRole::Server ? NET_SNAPSHOT_RATE
                                                             : NET_COMMAND_RATE);
    if (m_sendAccumulator < interval) return;

    // Carried over rather than zeroed, so a slightly long frame does not drift
    // slow; capped at one interval, so a stalled one sends once rather than
    // firing off the whole burst it now owes.
    m_sendAccumulator = std::min(m_sendAccumulator - interval, interval);

    if (m_role == NetRole::Server) sendAsServer(scene, tick);
    else                           sendAsClient();
}

NetSnapshotStats NetSession::sendSnapshot(Scene& scene, const NetSchema& schema, Peer& peer,
                                          uint32_t tick) {
    // The connection's next sequence is the snapshot's identity, which holds
    // because a peer gets one datagram per frame - and is why the budget defers
    // entities rather than spilling into a second packet.
    const uint16_t sequence = peer.connection.nextSequence();

    std::vector<uint8_t> payload;
    BitWriter writer(payload, MAX_PAYLOAD);
    writeMessageHeader(writer, NetMessage::Snapshot);
    writer.u32(tick);
    // Which of this player's commands the snapshot has run - the only thing
    // that lets them compare. Without it a client knows what the server thinks
    // but not which of its own moments to hold it against.
    writer.u32(peer.commands.newestRunTick());

    // Written before the world, so what must arrive takes its room first. A
    // spawn a client never hears about is worse than one snapshot of slightly
    // stale positions.
    peer.reliable.write(writer);

    NetBudget budget;
    budget.bytes = static_cast<uint32_t>(MAX_PAYLOAD - writer.byteCount());
    budget.owner = peer.entity;

    std::vector<uint8_t> body;
    const NetSnapshotStats cost =
        writeSnapshot(scene, schema, sequence, budget, peer.baseline, body);

    writer.append(body.data(), body.size() * 8u);
    writer.finish();

    // The budget above is sized so this cannot happen. If it ever does it is a
    // fault here, and saying so beats arriving as a packet the far end reports
    // as unreadable.
    if (writer.overflowed()) {
        LOG_ERROR("A snapshot body of %zu bytes did not fit the %u it was budgeted; "
                  "sending nothing to player %u this tick",
                  body.size(), budget.bytes, peer.id);
        return cost;
    }

    peer.connection.frame(payload.data(), payload.size(), m_datagram);
    m_socket.send(peer.connection.peer(), m_datagram.data(), m_datagram.size());
    return cost;
}

void NetSession::sendAsServer(Scene& scene, uint32_t tick) {
    const NetSchema& schema = NetSchema::get();

    // Totalled across peers rather than assigned per peer: what a host wants to
    // read is what this frame cost, not what the last player in the list cost.
    NetSnapshotStats frame;

    for (auto& peer : m_peers) {
        // Nothing but the Welcome until this address has answered one. A client
        // that lost it says Hello again and is answered there; an address that
        // never asked is one that cannot hear, so the world is not sent to it.
        if (!peer->answered) continue;

        const NetSnapshotStats one = sendSnapshot(scene, schema, *peer, tick);

        // Summed, because a round costs what every peer was sent. Except how
        // many entities were considered, which is the world and the same for
        // each: summed, two players would report twice the world.
        frame.entitiesConsidered  = one.entitiesConsidered;
        frame.entitiesWritten    += one.entitiesWritten;
        frame.componentsWritten  += one.componentsWritten;
        frame.entitiesDeferred   += one.entitiesDeferred;
        frame.entitiesLost       += one.entitiesLost;
        frame.bytesWritten       += one.bytesWritten;
    }

    m_lastSnapshot = frame;
}

void NetSession::sendAsClient() {
    if (!m_server) return;

    // Not before the first tick has run, because the count sent with the hello
    // has to be the project's and the project sets it from a behavior.
    if (!m_ticked) return;

    std::vector<uint8_t> payload;
    BitWriter writer(payload, UdpSocket::MAX_DATAGRAM - NetConnection::HEADER_BYTES);
    writeMessageHeader(writer, m_localPlayer == NO_PLAYER ? NetMessage::Hello
                                                          : NetMessage::Command);
    if (m_localPlayer == NO_PLAYER) {
        // Still joining. The Hello repeats every frame until it is answered,
        // which is what makes a join survive a lost first packet without a
        // retry timer of its own.
        writer.u32(NetSchema::get().fingerprint());
        writer.u8(static_cast<uint8_t>(m_actionCount));
        writer.u64(m_world);
    } else {
        // The token proves the seat, then the acknowledgement, the moment
        // being drawn, and the input. Nothing a client says has to arrive, so
        // it writes no reliable block of its own.
        writer.u64(m_token);
        m_reliable.write(writer);

        // Which server tick this end is looking at, whole and fraction, before
        // the commands so it reads even when there are none. Only the client
        // knows it: it already accounts for the trip, the delay and the frame.
        const float drawn = m_interpolation.renderTick();
        const uint32_t whole = drawn > 0.0f ? static_cast<uint32_t>(drawn) : 0u;
        writer.u32(whole);
        writer.bits(static_cast<uint32_t>((drawn - static_cast<float>(whole)) * 255.0f), 8);

        writeCommands(writer, m_prediction.unacknowledged, m_actionCount);
    }
    writer.finish();

    m_server->frame(payload.data(), payload.size(), m_datagram);
    m_socket.send(m_server->peer(), m_datagram.data(), m_datagram.size());
}

EntityId NetSession::spawn(Scene& scene, ResourceManager& resources,
                           const std::string& prefab, const Transform& at) {
    if (m_role == NetRole::Client) {
        LOG_ERROR("A client asked to spawn '%s'; what exists is the server's to decide",
                  prefab.c_str());
        return EntityId{};
    }

    const EntityId root = Prefab::instantiate(scene, resources, prefab, at);
    if (!root) {
        LOG_ERROR("Could not build '%s'", prefab.c_str());
        return EntityId{};
    }
    if (m_role == NetRole::Offline) return root;

    NetSpawn message;
    message.slot   = root.slot();
    message.prefab = prefab;
    message.at     = at;

    std::vector<uint8_t> bytes;
    BitWriter writer(bytes, NetReliable::MAX_MESSAGE);
    if (!writeSpawn(writer, message)) {
        LOG_ERROR("'%s' is too long a path to name in a spawn message (the limit is "
                  "%zu), so no client will see it", prefab.c_str(), NET_PREFAB_PATH_MAX);
        return root;
    }
    writer.finish();
    if (writer.overflowed()) {
        LOG_ERROR("'%s' does not fit a spawn message, so no client will see it",
                  prefab.c_str());
        return root;
    }
    broadcast(bytes.data(), bytes.size());
    return root;
}

void NetSession::despawn(Scene& scene, EntityId entity) {
    if (!scene.isAlive(entity)) return;
    if (m_role == NetRole::Client) {
        LOG_ERROR("A client asked to despawn an entity; that is the server's to decide");
        return;
    }

    if (m_role != NetRole::Offline) {
        std::vector<uint8_t> bytes;
        BitWriter writer(bytes, 16);
        writeDespawn(writer, entity.slot());
        writer.finish();
        broadcast(bytes.data(), bytes.size());
    }
    HierarchyOperations::destroyHierarchy(scene, entity);
}

void NetSession::broadcast(const uint8_t* bytes, size_t size) {
    for (auto& peer : m_peers) {
        if (peer->reliable.queue(bytes, size)) continue;

        // A full channel is a connection to give up on, not a message to retry.
        // Everything after it goes missing, and a snapshot naming the slot then
        // builds a bare entity - an invisible body nothing ever corrects.
        LOG_ERROR("Player %u has too many unconfirmed messages; the channel is full",
                  peer->id);
        peer->leaving = "its reliable channel filled";
    }
}

void NetSession::applySpawn(Scene& scene, ResourceManager& resources, BitReader& reader) {
    NetSpawn spawn;
    if (!readSpawn(reader, spawn)) {
        LOG_ERROR("A spawn message did not decode");
        return;
    }
    // Already built, because the message is repeated until confirmed and a
    // confirmation can be the packet that goes missing.
    if (scene.isAliveAtIndex(spawn.slot)) return;

    const EntityId root = scene.createEntityAt(spawn.slot);
    if (!root) {
        // The server has an entity here and this end cannot make one, so the
        // two worlds now differ by whatever it was. Said out loud, or a player
        // hunts something that was never built.
        LOG_ERROR("Slot %u could not be created for '%s'; this end is missing "
                  "what the server spawned", spawn.slot, spawn.prefab.c_str());
        return;
    }
    scene.add(root, Transform{spawn.at});
    scene.add(root, PrefabInstance{spawn.prefab, {}});
    if (!Prefab::instantiateInto(scene, resources, spawn.prefab, root)) {
        LOG_ERROR("The server built '%s' and this end could not", spawn.prefab.c_str());
        HierarchyOperations::destroyHierarchy(scene, root);
    }
}

void NetSession::applyDespawn(Scene& scene, BitReader& reader) {
    const uint32_t slot = reader.u32();
    if (reader.failed()) {
        LOG_ERROR("A despawn message did not decode");
        return;
    }
    // Already gone, because the message is repeated until confirmed.
    if (!scene.isAliveAtIndex(slot)) return;

    // Read before the destroy, not after: past it the slot names whatever the
    // allocator has made of it, and the track this was meant to drop is keyed
    // on the entity that was there.
    const EntityId gone = scene.entityAt(slot);
    HierarchyOperations::destroyHierarchy(scene, gone);
    m_interpolation.forget(gone);
}

void NetSession::applyMessage(Scene& scene, ResourceManager& resources,
                              const std::vector<uint8_t>& message) {
    BitReader reader(message.data(), message.size());
    const auto kind = static_cast<NetEvent>(reader.u8());
    if (reader.failed()) {
        LOG_ERROR("A reliable message arrived too short to name what it is");
        return;
    }

    switch (kind) {
        case NetEvent::Spawn:   applySpawn(scene, resources, reader); break;
        case NetEvent::Despawn: applyDespawn(scene, reader);          break;
        default:
            // Both ends agreed a schema fingerprint before playing, so a kind
            // this end cannot name means the stream is being read at the wrong
            // offset rather than that the other end is newer.
            LOG_ERROR("A reliable message named event %u, which this build does "
                      "not know", static_cast<unsigned>(kind));
            break;
    }
}

void NetSession::beginRewind(Scene& scene, EntityId shooter) {
    m_rewound.clear();
    if (m_role != NetRole::Server) return;

    // Which moment this player was drawing. A host plays on the same machine as
    // the server, so it is looking at the present and there is nothing to undo.
    const Peer* firing = nullptr;
    for (const auto& peer : m_peers) {
        if (peer->entity == shooter) { firing = peer.get(); break; }
    }
    if (!firing) return;

    // Clamped, because it arrived from a client - which bounds how far back a
    // shot may reach. A claim past what is remembered is a slow link rather
    // than a lie, so it is pulled to the edge rather than refused.
    const float oldest = static_cast<float>(m_serverTick)
                       - static_cast<float>(NetRewind::HISTORY_TICKS - 1);
    const float when   = std::max(oldest,
                                  std::min(firing->renderTick, static_cast<float>(m_serverTick)));

    const auto rewindOne = [&](EntityId entity) {
        if (!entity || entity == shooter) return;
        if (!scene.isAlive(entity) || !scene.has<Transform>(entity)) return;

        glm::vec3 position;
        glm::quat rotation;
        if (!m_rewind.poseAt(entity, when, position, rotation)) return;

        Transform& at = scene.get<Transform>(entity);
        m_rewound.push_back({entity, at});
        at.position = position;
        at.rotation = rotation;
    };
    for (const auto& peer : m_peers) rewindOne(peer->entity);
}

void NetSession::endRewind(Scene& scene) {
    for (const Rewound& was : m_rewound) {
        if (!scene.isAlive(was.entity) || !scene.has<Transform>(was.entity)) continue;
        scene.get<Transform>(was.entity) = was.transform;
    }
    m_rewound.clear();
}

std::string NetSession::describe() const {
    if (m_role == NetRole::Offline) return "offline";

    char line[256];
    if (m_role == NetRole::Server) {
        float worst = 0.0f;
        float worstLoss = 0.0f;
        for (const auto& peer : m_peers) {
            worst     = std::max(worst, peer->connection.roundTrip());
            worstLoss = std::max(worstLoss, peer->connection.lossFraction());
        }
        std::snprintf(line, sizeof(line),
                      "hosting on %u, %zu player(s), worst ping %.0f ms, "
                      "worst loss %.1f%%, last round %u B for %u entries of %u "
                      "entities (%u deferred), refused %u mismatched and %u full",
                      m_socket.localAddress().port, playerCount(), worst * 1000.0f,
                      worstLoss * 100.0f,
                      m_lastSnapshot.bytesWritten, m_lastSnapshot.entitiesWritten,
                      m_lastSnapshot.entitiesConsidered, m_lastSnapshot.entitiesDeferred,
                      m_refusedMismatch, m_refusedFull);
        return line;
    }

    if (m_role == NetRole::Disconnected) {
        std::snprintf(line, sizeof(line), "disconnected - %s; the world is holding still",
                      m_lastError.empty() ? "no reason given" : m_lastError.c_str());
        return line;
    }

    if (!isPlaying()) {
        std::snprintf(line, sizeof(line), "joining %s", m_server ? m_server->peer().toString().c_str()
                                                                 : "nowhere");
        return line;
    }
    std::snprintf(line, sizeof(line),
                  "player %u, ping %.0f ms (+/- %.0f), losing %.1f%%, prediction out "
                  "by %.0f mm, drawing %.0f ticks behind, smoothing %zu entities",
                  m_localPlayer, roundTrip() * 1000.0f,
                  m_server ? m_server->roundTripVariation() * 1000.0f : 0.0f,
                  m_server ? m_server->lossFraction() * 100.0f : 0.0f,
                  m_prediction.error * 1000.0f, renderDelay(), m_interpolation.tracked());
    return line;
}

std::vector<NetSession::PlayerInfo> NetSession::players() const {
    std::vector<PlayerInfo> out;
    if (m_role == NetRole::Server) {
        for (const auto& peer : m_peers) {
            out.push_back({peer->id, peer->entity, peer->connection.roundTrip(), false});
        }
    } else if (isPlaying()) {
        out.push_back({m_localPlayer, m_localEntity, roundTrip(), true});
    }
    return out;
}

} // namespace Vkm::Engine
