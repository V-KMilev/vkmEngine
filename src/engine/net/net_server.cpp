#define VKM_LOG_CATEGORY "NET"

#include "net/net_server.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

#include "logger.h"

#include "ecs/scene.h"
#include "net/net_session.h"
#include "net/wire/codecs.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

/**
 * @brief One seated player, from the server's side.
 *
 * Made only once the address has proven it can hear (see greet). Allocated per
 * connection: per-seat state reused across players would hand the next one
 * deltas against a world they never held.
 */
struct NetServer::Peer {
    NetConnection    connection;
    NetBaseline      baseline;
    NetCommandBuffer commands;
    InputCommand     current;            ///< What this player's entity runs this tick.
    PlayerId         id = NO_PLAYER;     ///< Never NO_PLAYER once made.
    EntityId         entity;
    const char*      leaving = nullptr;  ///< Why this peer is going, once it is.
    uint64_t         token = 0;          ///< What proves a packet is this player's.

    /// Whether a Command has come back, which says the Welcome arrived.
    bool             answered = false;

    /**
     * @brief Which server tick this client said it was drawing, when it had just
     *        made the command numbered drewAtCommand.
     *
     * In this server's clock, which the client's interpolation runs in. A
     * running command was aimed at this, less how many ticks older it is than
     * drewAtCommand.
     */
    double           renderTick    = 0.0;
    uint32_t         drewAtCommand = 0;  ///< Client tick of the newest command in that packet.
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
    m_cookie.rekey();
}

void NetServer::close() {
    // A told peer frees the seat now instead of at timeout. Each carries its
    // seat's token, or it is dropped as a forgery.
    for (auto& peer : m_peers) sendBare(m_core, peer->connection, NetMessage::Goodbye, peer->token);

    m_peers.clear();
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
}

void NetServer::receive(Scene& scene, ResourceManager& resources) {
    NetAddress from;
    // Bounded rather than drained - see NetCore::MAX_READS_PER_FRAME.
    for (size_t read = 0; read < NetCore::MAX_READS_PER_FRAME; ++read) {
        if (!m_core.socket.receive(m_core.datagram, from)) break;

        // Read before the connection accepts it: accepting moves the window,
        // and one forged datagram numbered far ahead would refuse every real one.
        BitReader       reader  = receivedPayload(m_core);
        NetMessage      message = NetMessage::Hello;
        uint64_t        token   = 0;
        const NetSender sender  = readMessageHeader(reader, message, token);
        if (sender == NetSender::Unknown) continue;

        Peer* peer = findPeer(from);

        // Told, not ignored: an unanswered join looks like a firewall.
        if (sender == NetSender::OtherBuild) {
            if (message != NetMessage::Hello || peer) continue;
            if (m_refusedMismatch == 0) {
                LOG_ERROR(
                    "Refusing a join: a different build, speaking another version of "
                        "the wire than this one's %u",
                    NET_PROTOCOL_VERSION
                );
            }
            ++m_refusedMismatch;
            refuse(from, NetRefusal::Mismatch, token);
            continue;
        }
        if (!peer || token != peer->token) {
            // The one thing an address without a seat may say.
            if (message != NetMessage::Hello) continue;
            if (!peer) {
                greet(from, token, reader, scene, resources);
            } else if (!peer->answered && m_cookie.accepts(from, token, m_seconds)) {
                // Told two tokens across a window's turn, a join can be seated
                // by one and hold the other; either proves it, so the seat
                // takes the one it now holds.
                peer->token = token;
                welcome(*peer, from);
            }
            continue;
        }

        m_core.acknowledged.clear();
        const bool accepted = peer->connection.accept(
            m_core.datagram.data(),
            m_core.datagram.size(),
            m_core.scratch,
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
        if (m_peers[i]->leaving) {
            dropPeer(scene, resources, i, m_peers[i]->leaving);
        } else if (m_peers[i]->connection.timedOut()) {
            dropPeer(scene, resources, i, "stopped answering");
        }
    }
}

void NetServer::acceptCommands(Peer& peer, BitReader& reader) {
    const uint32_t drawnWhole    = reader.u32();
    const uint32_t drawnFraction = reader.bits(8);
    if (reader.failed()) return;

    std::vector<InputCommand>& commands = m_received;
    if (!readCommands(reader, m_core.actionCount, commands)) {
        LOG_ERROR("Player %u sent a command packet that did not decode", peer.id);
        return;
    }
    for (const InputCommand& command : commands) peer.commands.accept(command);

    // What the client drew belongs to its newest command; with none it says nothing.
    if (commands.empty()) return;
    peer.renderTick    = static_cast<double>(drawnWhole) + static_cast<double>(drawnFraction) / 255.0;
    peer.drewAtCommand = commands.back().tick;
}

bool NetServer::vetHello(
    const NetAddress& from,
    uint64_t echoed,
    BitReader& reader,
    uint32_t& actions,
    uint64_t& actionNames
) {
    const uint32_t fingerprint = reader.u32();
    actions                    = reader.u8();
    actionNames                = reader.u64();
    const uint64_t world       = reader.u64();
    const uint32_t tickRate    = reader.u16();
    if (reader.failed()) return false;

    if (fingerprint != NetSchema::get().fingerprint()) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR(
                "Refusing a join: a different build or project. This end replicates: %s",
                NetSchema::get().describe().c_str()
            );
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch, echoed);
        return false;
    }

    // A generated world says zero and is compared against nothing.
    if (m_core.world != 0 && world != 0 && world != m_core.world) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR(
                "Refusing a join: a different world. This end loaded %016llx, the other %016llx",
                static_cast<unsigned long long>(m_core.world),
                static_cast<unsigned long long>(world)
            );
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch, echoed);
        return false;
    }

    // A command is one tick, so rates must match: the pacing bends a clock by
    // at most NetPacing::MAX_DILATION, not by a factor.
    if (tickRate != m_core.tickRate) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR(
                "Refusing %s: it ticks at %u a second and this game at %u",
                from.toString().c_str(),
                tickRate,
                m_core.tickRate
            );
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::TickRate, echoed);
        return false;
    }

    // The codec bounds it too; a refusal beats every packet being discarded.
    if (actions > MAX_INPUT_ACTIONS) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR(
                "Refusing %s: it claims %u actions and a command holds %u",
                from.toString().c_str(),
                actions,
                MAX_INPUT_ACTIONS
            );
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch, echoed);
        return false;
    }

    // The count is every command's field width, so every player must agree.
    const size_t seated = m_peers.size();
    if (seated > 0 && actions != m_core.actionCount) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR(
                "Refusing %s: it defines %u actions and this game has %u",
                from.toString().c_str(),
                actions,
                m_core.actionCount
            );
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch, echoed);
        return false;
    }

    // The same count in another order misreads every bit. Zero compares against nothing.
    if (m_core.actionNames != 0 && actionNames != 0 && actionNames != m_core.actionNames) {
        if (m_refusedMismatch == 0) {
            LOG_ERROR(
                "Refusing %s: it assigns its actions to command slots in a different order",
                from.toString().c_str()
            );
        }
        ++m_refusedMismatch;
        refuse(from, NetRefusal::Mismatch, echoed);
        return false;
    }

    if (seated >= m_maxPlayers) {
        ++m_refusedFull;
        refuse(from, NetRefusal::Full, echoed);
        return false;
    }
    return true;
}

void NetServer::greet(
    const NetAddress& from,
    uint64_t echoed,
    BitReader& hello,
    Scene& scene,
    ResourceManager& resources
) {
    uint32_t actions     = 0;
    uint64_t actionNames = 0;
    if (!vetHello(from, echoed, hello, actions, actionNames)) return;

    // A source address is forgeable, so it is seated only when it echoes its
    // token - a keyed hash of the address, so nothing is held until then.
    if (!m_cookie.accepts(from, echoed, m_seconds)) {
        challenge(from);
        return;
    }

    // The proving Hello is the connection's first packet, for the Welcome to
    // acknowledge; accepted before any send reuses its buffer.
    auto peer = std::make_unique<Peer>();
    peer->connection.open(from);
    m_core.acknowledged.clear();
    peer->connection.accept(
        m_core.datagram.data(),
        m_core.datagram.size(),
        m_core.scratch,
        m_core.acknowledged
    );

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
    m_core.actionCount = actions;
    if (m_core.actionNames == 0) m_core.actionNames = actionNames;
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

void NetServer::challenge(const NetAddress& to) {
    m_unseated.open(to);
    sendBare(m_core, m_unseated, NetMessage::Challenge, m_cookie.issue(to, m_seconds));
}

void NetServer::welcome(Peer& peer, const NetAddress& to) {
    BitWriter writer(m_core.payload, 64);
    writeMessageHeader(writer, NetMessage::Welcome, peer.token);
    writer.u16(peer.id);
    writer.u32(peer.entity.slot());
    writer.finish();

    peer.connection.frame(m_core.payload.data(), m_core.payload.size(), m_core.datagram);
    m_core.socket.send(to, m_core.datagram.data(), m_core.datagram.size());
}

void NetServer::refuse(const NetAddress& from, NetRefusal reason, uint64_t token) {
    m_unseated.open(from);

    BitWriter writer(m_core.payload, 32);
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
    m_peers.erase(m_peers.begin() + static_cast<long>(index));
}

NetServer::Peer* NetServer::findPeer(const NetAddress& address) {
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

        const NetSnapshotStats one = sendSnapshot(scene, schema, *peer, tick);

        // Summed over peers, except entities considered, which is the world.
        frame.entitiesConsidered  = one.entitiesConsidered;
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
    BitWriter writer(payload, NetConnection::MAX_PAYLOAD);
    writeMessageHeader(writer, NetMessage::Snapshot, peer.token);
    writer.u32(tick);
    // The newest command run, which the client reconciles against.
    writer.u32(peer.commands.newestRunTick());
    // The newest arrived, run or not, so the client stops resending it.
    writer.u32(peer.commands.newestSequence());
    // How low the queue ran since the last snapshot, for the client's pacing.
    writer.bits(
        static_cast<uint32_t>(std::min<size_t>(peer.commands.lowWater(), NET_QUEUE_DEPTH_MAX)),
        NET_QUEUE_DEPTH_BITS
    );
    peer.commands.restartLowWater();

    // What the headers leave; what does not fit is deferred, never a second datagram.
    NetBudget budget;
    budget.bytes = static_cast<uint32_t>(NetConnection::MAX_PAYLOAD - writer.byteCount());
    budget.owner = peer.entity;

    const NetSnapshotStats cost =
        writeSnapshot(scene, schema, m_silence, sequence, budget, peer.baseline, writer);
    writer.finish();

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
    const double drawn  = firing->renderTick - queued;

    // A client's claim, so clamped; past memory is a slow link, not a lie, so
    // it is pulled to the edge rather than refused.
    const double oldest = static_cast<double>(m_core.serverTick)
        - static_cast<double>(NetRewind::HISTORY_TICKS - 1);
    const double when   = std::max(oldest, std::min(drawn, static_cast<double>(m_core.serverTick)));

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

std::string NetServer::describe() const {
    float    worst     = 0.0f;
    float    worstLoss = 0.0f;
    uint32_t repeated  = 0;
    uint32_t skipped   = 0;
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
