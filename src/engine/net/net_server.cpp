#define VKM_LOG_CATEGORY "NET"

#include "net/net_server.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

#include "vkm_log/logger.h"

#include "ecs/scene.h"
#include "net/latency/command_buffer.h"
#include "net/latency/pacing.h"
#include "net/message_bodies.h"
#include "net/net_core.h"
#include "net/replication/silence.h"
#include "net/wire/codecs.h"
#include "net/wire/project_path.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

namespace {

constexpr float LOADING_TIMEOUT_SECONDS = 30.0f;  ///< How long a player loading the scene the session moved
                                                  ///< to may be silent: a load can hold its frames, and with
                                                  ///< them every packet it would send.

} // namespace

/**
 * @brief One seated player, from the server's side.
 *
 * Made only once the address has proven it can hear (see greet). Allocated per
 * connection: per-seat state reused across players would hand the next one
 * deltas against a world they never held.
 */
struct NetServer::Peer {
    Network::Connection connection;
    NetBaseline         baseline;
    NetCommandBuffer    commands;
    Network::Inbox      inbox;                ///< This player's messages, each handed on once.
    InputCommand        current;              ///< What this player's entity runs this tick.
    PlayerId            id      = NO_PLAYER;  ///< Never NO_PLAYER once made.
    EntityId            entity;
    const char*         leaving = nullptr;    ///< Why this peer is going, once it is.
    uint64_t            token   = 0;          ///< What proves a packet is this player's.

    bool                answered = false;  ///< Whether a Command has come back, which says the Welcome arrived.

    bool                toldTooLarge = false;  ///< Whether this player has been told of, once, as driving an
                                               ///< entity too large to describe.

    uint8_t             worldGeneration = 0;  ///< Which of the session's worlds this client last said it holds
                                              ///< (NetCore::worldGeneration).

    double              renderTick    = 0.0;  ///< Which server tick this client said it was drawing when it had
                                              ///< just made the command numbered drewAtCommand, in this server's
                                              ///< clock, which the client's interpolation runs in. A running
                                              ///< command was aimed at this, less how many ticks older it is
                                              ///< than drewAtCommand.
    uint32_t            drewAtCommand = 0;    ///< Client tick of the newest command in that packet.
};

NetServer::NetServer(NetCore& core)
    : m_core(core)
{
}

NetServer::~NetServer() = default;

void NetServer::onSpawn(SpawnPlayer spawn, DespawnPlayer despawn) {
    m_spawn   = std::move(spawn);
    m_despawn = std::move(despawn);
}

void NetServer::open(uint32_t maxPlayers) {
    m_maxPlayers = maxPlayers;
    // The clock starts again at zero, so under the last key the last session's tokens would prove a join.
    m_cookie = Network::JoinCookie{};
}

void NetServer::close() {
    // A told peer frees the seat now instead of at timeout. Each carries its
    // seat's token, or it is dropped as a forgery.
    for (auto& peer : m_peers) sendBare(m_core, peer->connection, NetMessage::Goodbye, peer->token);

    m_peers.clear();
    m_messages.clear();
    m_rewind.clear();
    m_rewound.clear();
    m_seconds      = 0.0;
    m_warnedScale  = false;
    m_lastSnapshot = NetSnapshotStats{};
}

void NetServer::advance(float seconds) {
    m_seconds += static_cast<double>(seconds);
    for (auto& peer : m_peers) peer->connection.advance(seconds);
}

void NetServer::beginTick(uint32_t tick) {
    for (auto& peer : m_peers) {
        peer->current = peer->commands.take(tick);
    }
}

void NetServer::endTick(const Scene& scene, uint32_t tick) {
    // Only players: they are what players shoot at, and bounded by seats.
    for (const auto& peer : m_peers) {
        const Transform* at = scene.tryGet<Transform>(peer->entity);
        if (!at) continue;
        m_rewind.record(peer->entity, tick, at->position, at->rotation);
    }
}

const InputCommand* NetServer::commandFor(EntityId entity) const {
    for (const auto& peer : m_peers) {
        if (peer->entity == entity) return &peer->current;
    }
    return nullptr;
}

void NetServer::forgetWorld() {
    m_rewind.clear();
    m_rewound.clear();

    // Baselines are keyed by entity id too, so would delta against a gone world.
    for (auto& peer : m_peers) peer->baseline.clear();
    m_loaded.clear();
}

void NetServer::adoptWorld(const Scene& scene) {
    // Classified in one pass: netSilence alone walks every ragdoll's bodies for each entity.
    m_silence.build(scene);
    m_loaded.clear();
    scene.forEachEntity([&](EntityId entity) {
        if (m_silence.of(entity) != NetSilence::Local) m_loaded.push_back(entity);
    });
}

template <typename Log>
void NetServer::refuseMismatch(const Network::Address& from, NetRefusal reason, uint64_t token, Log&& log) {
    if (m_refusedMismatch == 0) log();
    ++m_refusedMismatch;
    refuse(from, reason, token);
}

void NetServer::receive(Scene& scene, ResourceManager& resources) {
    Network::Address from;
    // Bounded rather than drained - see NetCore::MAX_READS_PER_FRAME.
    for (size_t read = 0; read < NetCore::MAX_READS_PER_FRAME; ++read) {
        if (!m_core.socket.receive(m_core.datagram, from)) break;

        // Read before the connection accepts it: accepting moves the window,
        // and one forged datagram numbered far ahead would refuse every real one.
        Network::BitReader reader = receivedPayload(m_core);
        NetMessage message = NetMessage::Hello;
        uint64_t token = 0;
        uint8_t version = 0;
        const NetSender sender = readMessageHeader(reader, message, token, &version);
        if (sender == NetSender::Unknown) continue;

        Peer* peer = findPeer(from);

        // Told, not ignored: an unanswered join looks like a firewall. Told only once it echoes its
        // token, as greet tells any address.
        if (sender == NetSender::OtherBuild) {
            if (message != NetMessage::Hello || peer) continue;
            if (!m_cookie.accepts(from, token, m_seconds)) {
                challenge(from, token, version);
                continue;
            }
            refuseMismatch(from, NetRefusal::Mismatch, token, [&] {
                LOG_ERROR(
                    "Refusing a join: a different build, speaking another version of "
                        "the wire than this one's %u",
                    NET_PROTOCOL_VERSION
                );
            });
            continue;
        }
        if (!peer || token != peer->token) {
            // The one thing an address without a seat may say.
            if (!peer && message == NetMessage::Hello) greet(from, token, reader, scene, resources);
            continue;
        }

        m_core.acknowledged.clear();
        const bool accepted = peer->connection.accept(
            m_core.datagram.data(),
            m_core.datagram.size(),
            m_core.acknowledged
        );
        if (!accepted) continue;
        for (uint16_t sequence : m_core.acknowledged) peer->baseline.confirm(sequence);

        switch (message) {
            case NetMessage::Hello:
                // A seated player saying Hello lost its Welcome.
                welcome(*peer, from);
                break;
            case NetMessage::Goodbye:
                // Marked: the sweep below is the one place a seat is given back.
                peer->leaving = "said goodbye";
                break;
            case NetMessage::Command:
                peer->answered = true;
                acceptCommands(*peer, reader);
                break;
            default:
                break;
        }
    }

    // After the read loop, so a leaving peer does not invalidate the list.
    for (size_t i = m_peers.size(); i-- > 0;) {
        const bool  loading = m_peers[i]->worldGeneration != m_core.worldGeneration;
        const float allowed = loading ? LOADING_TIMEOUT_SECONDS : Network::Connection::TIMEOUT_SECONDS;
        if (m_peers[i]->leaving) {
            dropPeer(scene, resources, i, m_peers[i]->leaving);
        } else if (m_peers[i]->connection.timedOut(allowed)) {
            dropPeer(scene, resources, i, "stopped answering");
        }
    }
}

void NetServer::acceptCommands(Peer& peer, Network::BitReader& reader) {
    CommandPreamble preamble;
    preamble.read(reader);
    if (reader.failed()) return;
    peer.worldGeneration = preamble.worldGeneration;

    std::vector<InputCommand>& commands = m_received;
    if (!readCommands(reader, m_core.actionCount, commands)) {
        LOG_ERROR("Player %u sent a command packet that did not decode", peer.id);
        return;
    }
    // Made for the world it is leaving, they would drive what the server spawned it in the next.
    const bool current = preamble.worldGeneration == m_core.worldGeneration;
    if (current) {
        for (const InputCommand& command : commands) peer.commands.accept(command);
    }

    // After the commands, in the room they left.
    const bool read = peer.inbox.read(reader, [&](const uint8_t* bytes, size_t size) {
        m_messages.push_back({peer.id, peer.entity, std::vector<uint8_t>(bytes, bytes + size)});
    });
    if (!read) LOG_ERROR("Player %u sent messages that did not decode", peer.id);

    // What the client drew belongs to its newest command; with none it says nothing.
    if (!current || commands.empty()) return;
    peer.renderTick    = preamble.drawnTick;
    peer.drewAtCommand = commands.back().tick;
}

bool NetServer::vetHello(
    const Network::Address& from,
    uint64_t echoed,
    Network::BitReader& reader,
    bool& current
) {
    HelloBody hello;
    hello.read(reader);
    if (reader.failed()) return false;
    const uint32_t actions  = hello.actions;
    const uint32_t tickRate = hello.tickRate;

    if (hello.schema != NetSchema::get().fingerprint()) {
        refuseMismatch(from, NetRefusal::Mismatch, echoed, [&] {
            LOG_ERROR(
                "Refusing a join: a different build or project. This end replicates: %s",
                NetSchema::get().describe().c_str()
            );
        });
        return false;
    }

    // A generated world says zero and is compared against nothing. A session that moved on to a
    // scene a client can be told of seats it, and tells it which.
    current = m_core.world == 0 || hello.world == 0 || hello.world == m_core.world;
    const bool movedOn = m_core.worldGeneration != 0 && isProjectJsonPath(m_core.worldPath);
    if (!current && !movedOn) {
        refuseMismatch(from, NetRefusal::Mismatch, echoed, [&] {
            LOG_ERROR(
                "Refusing a join: a different world. This end loaded %016llx, the other %016llx",
                static_cast<unsigned long long>(m_core.world),
                static_cast<unsigned long long>(hello.world)
            );
        });
        return false;
    }

    // A command is one tick, so rates must match: the pacing bends a clock by
    // at most NetPacing::MAX_DILATION, not by a factor.
    if (tickRate != m_core.tickRate) {
        refuseMismatch(from, NetRefusal::TickRate, echoed, [&] {
            LOG_ERROR(
                "Refusing %s: it ticks at %u a second and this game at %u",
                from.toString().c_str(),
                tickRate,
                m_core.tickRate
            );
        });
        return false;
    }

    // The codec bounds it too; a refusal beats every packet being discarded.
    if (actions > MAX_INPUT_ACTIONS) {
        refuseMismatch(from, NetRefusal::Mismatch, echoed, [&] {
            LOG_ERROR(
                "Refusing %s: it claims %u actions and a command holds %u",
                from.toString().c_str(),
                actions,
                MAX_INPUT_ACTIONS
            );
        });
        return false;
    }

    // The count is every command's field width and the names say what each slot is, so a client
    // must list this game's actions: both ends take them from project.json before any join.
    if (actions != m_core.actionCount || hello.actionNames != m_core.actionNames) {
        refuseMismatch(from, NetRefusal::Mismatch, echoed, [&] {
            LOG_ERROR(
                "Refusing %s: it lists %u input actions, or other ones, where this game lists %u",
                from.toString().c_str(),
                actions,
                m_core.actionCount
            );
        });
        return false;
    }

    if (m_peers.size() >= m_maxPlayers) {
        ++m_refusedFull;
        refuse(from, NetRefusal::Full, echoed);
        return false;
    }
    return true;
}

void NetServer::greet(
    const Network::Address& from,
    uint64_t echoed,
    Network::BitReader& hello,
    Scene& scene,
    ResourceManager& resources
) {
    // A source address is forgeable, so nothing is said to it but a Challenge until it echoes its
    // token - a keyed hash of the address, so nothing is held until then. A refusal sent sooner
    // would let a forged Hello turn a real client away.
    if (!m_cookie.accepts(from, echoed, m_seconds)) {
        challenge(from, echoed, NET_PROTOCOL_VERSION);
        return;
    }

    bool current = true;
    if (!vetHello(from, echoed, hello, current)) return;

    // The proving Hello is the connection's first packet, for the Welcome to
    // acknowledge; accepted before any send reuses its buffer.
    auto peer = std::make_unique<Peer>();
    peer->connection.open(from);
    // What it loaded is what the world was: what has gone since goes from it too.
    for (const EntityId loaded : m_loaded) peer->baseline.seed(loaded);
    m_core.acknowledged.clear();
    peer->connection.accept(m_core.datagram.data(), m_core.datagram.size(), m_core.acknowledged);

    // Taken only once the game accepts, so a declined join spends no number.
    const PlayerId id     = freePlayerId();
    const EntityId entity = (m_spawn && id != NO_PLAYER) ? m_spawn(scene, resources, id) : EntityId{};
    if (!entity) {
        LOG_INFO("Refusing %s: the game declined", from.toString().c_str());
        refuse(from, NetRefusal::Declined, echoed);
        return;
    }
    m_nextPlayer = static_cast<PlayerId>(id + 1);

    peer->token  = echoed;
    peer->id     = id;
    peer->entity = entity;
    // Any other number: it is told which world to load before anything is described to it.
    peer->worldGeneration =
        current ? m_core.worldGeneration : static_cast<uint8_t>(m_core.worldGeneration - 1);
    welcome(*peer, from);

    LOG_INFO(
        "Player %u joined from %s, driving entity %u (%zu of %u seats)",
        peer->id,
        from.toString().c_str(),
        peer->entity.slot(),
        m_peers.size() + 1,
        m_maxPlayers
    );
    m_peers.push_back(std::move(peer));
}

PlayerId NetServer::freePlayerId() const {
    // The count wraps, onto NO_PLAYER or a number still held.
    PlayerId id = m_nextPlayer;
    for (uint32_t tried = 0; tried <= std::numeric_limits<PlayerId>::max(); ++tried, ++id) {
        if (id == NO_PLAYER) continue;
        const bool held = std::any_of(
            m_peers.begin(),
            m_peers.end(),
            [id](const std::unique_ptr<Peer>& peer) { return peer->id == id; }
        );
        if (!held) return id;
    }
    return NO_PLAYER;
}

void NetServer::challenge(const Network::Address& to, uint64_t echo, uint8_t version) {
    m_unseated.open(to);

    Network::BitWriter writer(m_core.payload, 32);
    writeMessageHeader(writer, NetMessage::Challenge, m_cookie.issue(to, m_seconds), version);
    writer.u64(echo);
    writer.finish();

    m_unseated.frame(m_core.payload.data(), m_core.payload.size(), m_core.datagram);
    m_core.socket.send(to, m_core.datagram.data(), m_core.datagram.size());
}

void NetServer::welcome(Peer& peer, const Network::Address& to) {
    WelcomeBody welcome;
    welcome.player          = peer.id;
    welcome.slot            = peer.entity.slot();
    welcome.worldGeneration = m_core.worldGeneration;
    welcome.current         = peer.worldGeneration == m_core.worldGeneration;

    Network::BitWriter writer(m_core.payload, 64);
    writeMessageHeader(writer, NetMessage::Welcome, peer.token);
    welcome.write(writer);
    writer.finish();

    peer.connection.frame(m_core.payload.data(), m_core.payload.size(), m_core.datagram);
    m_core.socket.send(to, m_core.datagram.data(), m_core.datagram.size());
}

void NetServer::sendWorld(Peer& peer) {
    WorldBody world;
    world.generation  = m_core.worldGeneration;
    world.fingerprint = m_core.world;
    world.slot        = peer.entity.slot();
    world.path        = m_core.worldPath;

    Network::BitWriter writer(m_core.payload, 64 + NET_PROJECT_PATH_MAX);
    writeMessageHeader(writer, NetMessage::World, peer.token);
    world.write(writer);
    writer.finish();

    peer.connection.frame(m_core.payload.data(), m_core.payload.size(), m_core.datagram);
    m_core.socket.send(peer.connection.peer(), m_core.datagram.data(), m_core.datagram.size());
}

void NetServer::changeWorld(Scene& scene, ResourceManager& resources) {
    if (!isProjectJsonPath(m_core.worldPath)) {
        LOG_ERROR(
            "The world was replaced with no scene its players can be told to load ('%s'); they are let go",
            m_core.worldPath.c_str()
        );
    }
    // Zero is the session's first world, which seats no one whose entry scene differs.
    if (++m_core.worldGeneration == 0) ++m_core.worldGeneration;
    // Every entity went with the old world, and what each was still to be told to do. All before
    // the first spawn: the new world's ids can repeat the old's, and a spawn asks who drives what.
    for (auto& peer : m_peers) {
        peer->entity = {};
        peer->commands.clear();
    }
    for (size_t i = m_peers.size(); i-- > 0;) {
        Peer& peer = *m_peers[i];
        // It loads the scene file as a joiner does: what goes from it later goes from that peer too.
        for (const EntityId loaded : m_loaded) peer.baseline.seed(loaded);
        const EntityId entity = isProjectJsonPath(m_core.worldPath) && m_spawn
            ? m_spawn(scene, resources, peer.id)
            : EntityId{};
        if (!entity) {
            sendBare(m_core, peer.connection, NetMessage::Goodbye, peer.token);
            dropPeer(scene, resources, i, "it has no place in the new world");
            continue;
        }
        peer.entity = entity;
    }
    const char* path = m_core.worldPath.c_str();
    LOG_INFO("Moved the session to '%s'; %zu player(s) told to follow", path, m_peers.size());
}

void NetServer::refuse(const Network::Address& from, NetRefusal reason, uint64_t token) {
    m_unseated.open(from);

    Network::BitWriter writer(m_core.payload, 32);
    writeMessageHeader(writer, NetMessage::Refuse, token);
    writer.u8(static_cast<uint8_t>(reason));
    writer.finish();

    m_unseated.frame(m_core.payload.data(), m_core.payload.size(), m_core.datagram);
    m_core.socket.send(from, m_core.datagram.data(), m_core.datagram.size());
}

void NetServer::dropPeer(Scene& scene, ResourceManager& resources, size_t index, const char* why) {
    Peer& peer = *m_peers[index];
    LOG_INFO("Player %u left: %s", peer.id, why);
    if (m_despawn && peer.entity) m_despawn(scene, resources, peer.id, peer.entity);
    m_rewind.forget(peer.entity);
    // What they said this frame goes with them: their seat is gone, and DespawnPlayer has run.
    const auto theirs = [&](const PlayerMessageEvent& said) { return said.player == peer.id; };
    m_messages.erase(std::remove_if(m_messages.begin(), m_messages.end(), theirs), m_messages.end());
    m_peers.erase(m_peers.begin() + static_cast<long>(index));
}

NetServer::Peer* NetServer::findPeer(const Network::Address& address) {
    for (auto& peer : m_peers) {
        if (peer->connection.peer() == address) return peer.get();
    }
    return nullptr;
}

void NetServer::send(Scene& scene, uint32_t tick) {
    const NetSchema& schema = NetSchema::get();

    NetSnapshotStats frame;

    // What stays off the wire is a fact about the world, not about who is told.
    m_silence.build(scene);
    holdScalesToTheWire(scene);

    for (auto& peer : m_peers) {
        // Nothing but the Welcome until a Command shows it arrived: before that the
        // client does not know its own entity and would draw it late.
        if (!peer->answered) continue;

        // Until it holds the world, it is told which one, and nothing in it is described.
        if (peer->worldGeneration != m_core.worldGeneration) {
            sendWorld(*peer);
            continue;
        }

        const NetSnapshotStats one = sendSnapshot(scene, schema, *peer, tick);

        // Summed over peers, except entities considered, which is the world.
        frame.entitiesConsidered = one.entitiesConsidered;
        frame.entitiesWritten    += one.entitiesWritten;
        frame.componentsWritten  += one.componentsWritten;
        frame.entitiesDeferred   += one.entitiesDeferred;
        frame.entitiesLost       += one.entitiesLost;
        frame.bytesWritten       += one.bytesWritten;
    }

    m_lastSnapshot = frame;
}

NetSnapshotStats NetServer::sendSnapshot(Scene& scene, const NetSchema& schema, Peer& peer, uint32_t tick) {
    // The connection's next sequence is the snapshot's identity.
    const uint16_t sequence = peer.connection.nextSequence();

    std::vector<uint8_t>& payload = m_core.payload;
    Network::BitWriter writer(payload, Network::Connection::MAX_PAYLOAD);
    const size_t lowWater = std::min<size_t>(peer.commands.lowWater(), NET_QUEUE_DEPTH_MAX);
    peer.commands.restartLowWater();

    SnapshotHeader header;
    header.worldGeneration = m_core.worldGeneration;
    header.serverTick      = tick;
    header.confirmedTick   = peer.commands.newestRunTick();
    header.heard           = peer.commands.newestSequence();
    header.lowWater        = static_cast<uint32_t>(lowWater);
    header.messages        = peer.inbox.acknowledge();
    writeMessageHeader(writer, NetMessage::Snapshot, peer.token);
    header.write(writer);

    // What the headers leave; what does not fit is deferred, never a second datagram.
    NetBudget budget;
    budget.bytes = static_cast<uint32_t>(Network::Connection::MAX_PAYLOAD - writer.byteCount());
    budget.owner = peer.entity;

    const NetSnapshotStats cost =
        writeSnapshot(scene, schema, m_silence, sequence, budget, peer.baseline, writer);
    writer.finish();
    if (cost.ownerTooLarge && !peer.toldTooLarge) {
        peer.toldTooLarge = true;
        LOG_ERROR(
            "Player %u's entity %u is larger than a snapshot carries, so its state is never sent; "
            "the rest of the world still is",
            peer.id,
            peer.entity.slot()
        );
    }

    // The budget above is sized so this cannot happen.
    if (writer.overflowed()) {
        LOG_ERROR(
            "A snapshot did not fit the %u bytes it was budgeted; sending nothing to player %u this tick",
            budget.bytes,
            peer.id
        );
        return cost;
    }

    peer.connection.frame(payload.data(), payload.size(), m_core.datagram);
    m_core.socket.send(peer.connection.peer(), m_core.datagram.data(), m_core.datagram.size());
    return cost;
}

void NetServer::holdScalesToTheWire(Scene& scene) {
    scene.forEach<Transform>([&](EntityId entity, Transform& at) {
        if (m_silence.of(entity) != NetSilence::None) return;
        const glm::vec3 said = netScale(at.scale);
        // Compared as written, so a NaN, which equals nothing, is caught too.
        if (said == at.scale) return;
        if (!m_warnedScale) {
            m_warnedScale = true;
            LOG_WARNING(
                "Entity %u has a scale the wire cannot carry; it is held to %.0f either "
                    "way, here as on every client, and so is any like it",
                entity.slot(),
                static_cast<double>(NET_MAX_SCALE)
            );
        }
        at.scale = said;
    });
}

void NetServer::rewind(Scene& scene, EntityId shooter) {
    m_rewound.clear();

    // A shooter no peer drives is judged against the present.
    const Peer* firing = nullptr;
    for (const auto& peer : m_peers) {
        if (peer->entity == shooter) {
            firing = peer.get();
            break;
        }
    }
    if (!firing) return;

    // The running command was aimed as many ticks before the reported moment
    // as the queue is deep.
    const double queued = static_cast<double>(firing->drewAtCommand)
        - static_cast<double>(firing->commands.newestRunTick());
    const double drawn = firing->renderTick - queued;

    // A client's claim, so clamped; past memory is a slow link, not a lie, so
    // it is pulled to the edge rather than refused.
    const double oldest = static_cast<double>(m_core.serverTick)
        - static_cast<double>(NetRewind::HISTORY_TICKS - 1);
    const double when = std::max(oldest, std::min(drawn, static_cast<double>(m_core.serverTick)));

    const auto rewindOne = [&](EntityId entity) {
        if (entity == shooter) return;
        Transform* at = scene.tryGet<Transform>(entity);
        if (!at) return;

        glm::vec3 position;
        glm::quat rotation;
        if (!m_rewind.poseAt(entity, when, position, rotation)) return;

        m_rewound.push_back({entity, *at});
        at->position = position;
        at->rotation = rotation;
    };
    for (const auto& peer : m_peers) rewindOne(peer->entity);
}

void NetServer::restore(Scene& scene) {
    for (const Rewound& was : m_rewound) {
        if (Transform* at = scene.tryGet<Transform>(was.entity)) *at = was.transform;
    }
    m_rewound.clear();
}

std::vector<PlayerMessageEvent> NetServer::takeMessages() {
    std::vector<PlayerMessageEvent> taken;
    taken.swap(m_messages);
    return taken;
}

std::string NetServer::describe() const {
    float worst     = 0.0f;
    float worstLoss = 0.0f;
    uint32_t repeated = 0;
    uint32_t skipped  = 0;
    for (const auto& peer : m_peers) {
        worst     = std::max(worst, peer->connection.roundTrip());
        worstLoss = std::max(worstLoss, peer->connection.lossFraction());
        repeated += peer->commands.repeatedTicks();
        skipped  += peer->commands.skippedCommands();
    }

    char line[320];
    std::snprintf(
        line,
        sizeof(line),
        "hosting on %u, %zu player(s), worst ping %.0f ms, "
            "worst loss %.1f%%, last round %u B for %u entries of %u "
            "entities (%u deferred), refused %u mismatched and %u full, "
            "%u ticks run on a repeat and %u commands skipped",
        m_core.socket.localAddress().port,
        m_peers.size(),
        worst * 1000.0f,
        worstLoss * 100.0f,
        m_lastSnapshot.bytesWritten,
        m_lastSnapshot.entitiesWritten,
        m_lastSnapshot.entitiesConsidered,
        m_lastSnapshot.entitiesDeferred,
        m_refusedMismatch,
        m_refusedFull,
        repeated,
        skipped
    );
    return line;
}

} // namespace Vkm::Engine
