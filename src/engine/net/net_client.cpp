#define VKM_LOG_CATEGORY "NET"

#include "net/net_client.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <type_traits>
#include <utility>

#include <glm/gtc/constants.hpp>

#include "vkm_log/logger.h"

#include "base/container/slot_allocator.h"
#include "ecs/component/net/net_local.h"
#include "ecs/component/net/net_spawn.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "ecs/prefab_instances.h"
#include "io/scene/prefab.h"
#include "net/message_bodies.h"
#include "net/net_core.h"
#include "net/replication/snapshot.h"
#include "net/wire/project_path.h"
#include "net/wire/protocol.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

namespace {

constexpr size_t PREDICTION_HISTORY = 200;  ///< Predicted positions kept, in ticks: longer than
                                            ///< NET_COMMAND_MEMORY, a replay's reach, so an answer too old to
                                            ///< replay is still measured and reported as the link outrunning
                                            ///< the replay, not left uncorrected in silence.
static_assert(
    PREDICTION_HISTORY > NET_COMMAND_MEMORY,
    "an answer older than a replay can reach has to be recognized as one"
);

static_assert(
    std::is_same_v<decltype(NetSpawn::slots), Prefab::BuiltSlots>,
    "NetSpawn carries the prefab's own record of where an instance was built"
);

constexpr uint32_t LEASE_TICKS = 32;  ///< How long a lease outlives the last tick it was reported on. Must
                                      ///< outlive a bounce and not the push.

constexpr float CORRECTION_FLOOR = 0.003f;  ///< Below this a disagreement is quantization plus float noise,
                                            ///< not worth a replay.

constexpr float DRAW_CATCH_UP = 6.0f;  ///< How fast what is drawn catches up with what is simulated, per
                                       ///< second. Slow enough not to read as a jump, fast enough that nobody
                                       ///< shoots where the character is not.

constexpr float DRAW_SNAP = 4.0f;  ///< Past this the error is a teleport or a respawn, drawn at once rather
                                   ///< than eased.

/// A token for the first Hello, drawn from the operating system and never zero, so a Challenge or a Refuse
/// forged by someone who never saw the Hello can neither echo it nor carry it.
uint64_t drawToken() {
    std::random_device device;
    uint64_t token = 0;
    while (token == 0) token = (static_cast<uint64_t>(device()) << 32) | static_cast<uint64_t>(device());
    return token;
}

} // namespace

NetClient::NetClient(NetCore& core)
    : m_core(core)
{
}

NetClient::~NetClient() = default;

void NetClient::open(const Network::Address& server) {
    m_connection = std::make_unique<Network::Connection>();
    m_connection->open(server);
    m_token = drawToken();
}

void NetClient::close() {
    // Told, the server frees the seat now instead of at timeout. The goodbye
    // carries the seat's token, or it is dropped as a forgery.
    if (m_connection) sendBare(m_core, *m_connection, NetMessage::Goodbye, m_token);

    m_connection.reset();
    m_localPlayer    = NO_PLAYER;
    m_localEntity    = EntityId{};
    m_token          = 0;
    m_warnedTickJump = false;
    m_prediction.clear();
    m_interpolation.clear();
    m_outbox.clear();
    // A world it was told to load is the session's, and a later one's first move would be taken
    // for it.
    m_world = WorldToLoad{};
}

bool NetClient::sendMessage(const uint8_t* bytes, size_t size) {
    // From the Welcome on: before it, the server has no seat to hand a message to.
    return isPlaying() && m_outbox.push(bytes, size);
}

void NetClient::advance(float seconds) {
    if (m_connection) m_connection->advance(seconds);
}

bool NetClient::simulates(const Scene& scene, EntityId entity) const {
    if (!entity) return false;
    if (entity == m_localEntity) return true;

    for (const Lease& lease : m_prediction.leases) {
        if (lease.entity == entity) return true;
    }

    // What the server never names is this end's to run - its own, and what runs itself - but a
    // replay re-runs only what this end predicts, and would run those twice.
    if (m_prediction.replaying || !scene.isAlive(entity)) return false;
    if (!m_names.names(scene, entity)) return true;
    return static_cast<bool>(HierarchyOperations::findInSelfOrAncestors<NetLocal>(scene, entity));
}

void NetClient::ageLeases() {
    // A lapsed lease hands the body back to the track held all along.
    if (m_prediction.replaying) return;
    for (Lease& lease : m_prediction.leases) {
        if (lease.ticks > 0) --lease.ticks;
    }
    const auto lapsed = std::remove_if(
        m_prediction.leases.begin(),
        m_prediction.leases.end(),
        [](const Lease& lease) { return lease.ticks == 0; }
    );
    for (auto it = lapsed; it != m_prediction.leases.end(); ++it) {
        m_interpolation.hold(it->entity, false);
        m_prediction.released.push_back(it->entity);
    }
    m_prediction.leases.erase(lapsed, m_prediction.leases.end());
}

void NetClient::beginTick(const InputCommand& command) {
    m_prediction.unacknowledged.push_back(command);
    // Held until the server says it ran them, not just until sent: one frame
    // can run many ticks, and an unsent command is never run there.
    if (m_prediction.unacknowledged.size() > NET_COMMAND_MEMORY) {
        m_prediction.unacknowledged.erase(m_prediction.unacknowledged.begin());
    }
}

void NetClient::endTick(const Scene& scene, uint32_t tick) {
    const Transform* at = scene.tryGet<Transform>(m_localEntity);
    if (!at) return;

    m_prediction.predicted.push_back({tick, at->position});
    if (m_prediction.predicted.size() > PREDICTION_HISTORY) {
        m_prediction.predicted.erase(m_prediction.predicted.begin());
    }
}

void NetClient::forgetWorld() {
    m_prediction.clear();
    m_interpolation.clear();
    m_names.clear();
}

void NetClient::lease(const Scene& scene, const std::vector<EntityId>& entities) {
    for (EntityId entity : entities) {
        if (!entity || entity == m_localEntity) continue;
        // What this end runs already: leased, a replay would run it again.
        if (!m_names.names(scene, entity)) continue;
        if (HierarchyOperations::findInSelfOrAncestors<NetLocal>(scene, entity)) continue;

        const auto found = std::find_if(
            m_prediction.leases.begin(),
            m_prediction.leases.end(),
            [entity](const Lease& lease) { return lease.entity == entity; }
        );
        if (found != m_prediction.leases.end()) {
            found->ticks = LEASE_TICKS;
            continue;
        }
        m_prediction.leases.push_back({entity, LEASE_TICKS});
        m_prediction.acquired.push_back(entity);

        // Held, not forgotten: interpolating a predicted body would fight the
        // prediction. The track is what it slides back to on expiry.
        m_interpolation.hold(entity, true);
    }
}

void NetClient::restoreHeld(Scene& scene) {
    for (const Held& held : m_prediction.held) {
        Transform* at = scene.tryGet<Transform>(held.entity);
        if (!at) continue;
        *at = held.transform;
        if (held.hasBody) {
            if (Rigidbody* body = scene.tryGet<Rigidbody>(held.entity)) *body = held.body;
        }
        if (held.hasController) {
            if (CharacterController* controller = scene.tryGet<CharacterController>(held.entity)) {
                *controller = held.controller;
            }
        }
    }
}

void NetClient::reconcile(Scene& scene, uint32_t confirmedTick) {
    const glm::vec3 authoritative = scene.get<Transform>(m_localEntity).position;

    const auto found = std::find_if(
        m_prediction.predicted.begin(),
        m_prediction.predicted.end(),
        [confirmedTick](const Predicted& p) { return p.tick == confirmedTick; }
    );

    // Older than anything remembered, or already judged: the prediction stands.
    if (found == m_prediction.predicted.end()) {
        restoreHeld(scene);
        return;
    }

    // What the server has not run is exactly what a replay would run.
    const auto acknowledged = std::remove_if(
        m_prediction.unacknowledged.begin(),
        m_prediction.unacknowledged.end(),
        [confirmedTick](const InputCommand& command) { return command.tick <= confirmedTick; }
    );
    m_prediction.unacknowledged.erase(acknowledged, m_prediction.unacknowledged.end());

    m_prediction.error = glm::length(authoritative - found->position);

    if (m_prediction.error < CORRECTION_FLOOR) {
        restoreHeld(scene);
        m_prediction.predicted.erase(m_prediction.predicted.begin(), found + 1);
        return;
    }

    // A replay needs an unbroken run from the tick after the confirmed one;
    // with a gap the prediction stands and the next snapshot tries.
    if (m_prediction.unacknowledged.empty()
        || m_prediction.unacknowledged.front().tick != confirmedTick + 1) {
        // Once per outage: past NET_COMMAND_MEMORY the prediction is never
        // corrected again and nothing else would say why.
        if (!m_prediction.warnedNoReplay) {
            m_prediction.warnedNoReplay = true;
            LOG_WARNING(
                "The round trip has outrun the commands kept for a replay, so this "
                "end's prediction is no longer being corrected"
            );
        }
        restoreHeld(scene);
        m_prediction.predicted.erase(m_prediction.predicted.begin(), found + 1);
        return;
    }

    // What the picture owes is measured in endReplay, once the replay has run.
    m_prediction.replay         = m_prediction.unacknowledged;
    m_prediction.warnedNoReplay = false;

    // All of it: the replay records every tick again, and a second answer for
    // a tick would have the next snapshot correct an error twice.
    m_prediction.predicted.clear();
}

void NetClient::endReplay(const Scene& scene) {
    const bool ran = !m_prediction.replay.empty();
    m_prediction.replaying = false;
    m_prediction.replay.clear();
    if (!ran) return;

    // What the picture owes: where the player last saw each predicted body,
    // less where the replay put it.
    for (const Held& held : m_prediction.held) {
        const Transform* at = scene.tryGet<Transform>(held.entity);
        if (!at) continue;
        correctDraw(held.entity, held.transform.position + drawOffsetFor(held.entity), at->position);
    }
}

glm::vec3 NetClient::drawOffsetFor(EntityId entity) const {
    for (const DrawCorrection& correction : m_prediction.corrections) {
        if (correction.entity == entity) return correction.offset;
    }
    return glm::vec3(0.0f);
}

void NetClient::correctDraw(EntityId entity, const glm::vec3& drawn, const glm::vec3& simulated) {
    // Set, not added: what came in already counts the outstanding offset.
    glm::vec3 offset = drawn - simulated;
    if (glm::length(offset) > DRAW_SNAP) offset = glm::vec3(0.0f);

    for (DrawCorrection& correction : m_prediction.corrections) {
        if (correction.entity != entity) continue;
        correction.offset = offset;
        return;
    }
    if (glm::length(offset) < glm::epsilon<float>()) return;
    m_prediction.corrections.push_back({entity, offset});
}

void NetClient::applyDrawCorrection(Scene& scene, float deltaTime) {
    const float kept = std::max(0.0f, 1.0f - DRAW_CATCH_UP * deltaTime);
    for (DrawCorrection& correction : m_prediction.corrections) correction.offset *= kept;

    const auto settled = std::remove_if(
        m_prediction.corrections.begin(),
        m_prediction.corrections.end(),
        [&scene](const DrawCorrection& correction) {
            return glm::length(correction.offset) < glm::epsilon<float>()
                || !scene.isAlive(correction.entity);
        }
    );
    m_prediction.corrections.erase(settled, m_prediction.corrections.end());

    for (const DrawCorrection& correction : m_prediction.corrections) {
        offsetDrawn(scene, correction.entity, correction.offset);
    }
}

void NetClient::removeDrawCorrection(Scene& scene) {
    for (const DrawCorrection& correction : m_prediction.corrections) {
        offsetDrawn(scene, correction.entity, -correction.offset);
    }
}

void NetClient::offsetDrawn(Scene& scene, EntityId entity, const glm::vec3& by) {
    if (Transform* at = scene.tryGet<Transform>(entity)) at->position += by;
}

void NetClient::interpolate(Scene& scene, float deltaTime) {
    std::vector<glm::vec3> before;
    before.reserve(m_prediction.released.size());
    for (EntityId entity : m_prediction.released) {
        const Transform* at = scene.tryGet<Transform>(entity);
        before.push_back(at ? at->position + drawOffsetFor(entity) : glm::vec3(0.0f));
    }

    const auto ticks = static_cast<float>(m_core.tickRate);
    m_interpolation.apply(scene, deltaTime, ticks, snapshotRate(m_core.tickRate));

    for (size_t i = 0; i < m_prediction.released.size(); ++i) {
        const EntityId entity = m_prediction.released[i];
        const Transform* at   = scene.tryGet<Transform>(entity);
        if (!at) continue;
        correctDraw(entity, before[i], at->position);
    }
    m_prediction.released.clear();
}

bool NetClient::receive(Scene& scene, ResourceManager& resources) {
    Network::Address from;
    // Bounded rather than drained - see NetCore::MAX_READS_PER_FRAME.
    for (size_t read = 0; read < NetCore::MAX_READS_PER_FRAME; ++read) {
        if (!m_core.socket.receive(m_core.datagram, from)) break;
        if (!m_connection || !(from == m_connection->peer())) continue;

        // Checked before the connection sees it: a server address is as easy to forge.
        Network::BitReader reader = receivedPayload(m_core);
        NetMessage message = NetMessage::Hello;
        uint64_t token = 0;
        const NetSender sender = readMessageHeader(reader, message, token);
        if (sender == NetSender::Unknown) continue;

        // The token Hellos echo, from a server of any build, taken only from a Challenge echoing the one
        // this end holds: else a forger could pick it, and refuse this end in the server's name. The
        // connection does not see it, since nothing proves the sender.
        if (message == NetMessage::Challenge) {
            const uint64_t echo = reader.u64();
            if (m_localPlayer == NO_PLAYER && !reader.failed() && echo == m_token) m_token = token;
            continue;
        }

        // All else another build says that this end understands is a refusal.
        if (sender == NetSender::OtherBuild) {
            if (m_localPlayer != NO_PLAYER || message != NetMessage::Refuse || token != m_token) continue;
            return onRefuse(reader);
        }
        if (token != m_token) continue;

        m_core.acknowledged.clear();
        const bool accepted = m_connection->accept(
            m_core.datagram.data(),
            m_core.datagram.size(),
            m_core.acknowledged
        );
        if (!accepted) continue;

        bool open = true;
        switch (message) {
            case NetMessage::Welcome:  open = onWelcome(scene, reader); break;
            case NetMessage::Refuse:   open = onRefuse(reader);         break;
            case NetMessage::Goodbye:  open = onGoodbye();              break;
            case NetMessage::Snapshot: onSnapshot(scene, reader);       break;
            case NetMessage::World:    open = onWorld(reader);          break;
            default: break;
        }
        if (!open) return false;
    }

    if (m_connection && m_connection->timedOut()) {
        m_core.lastError = "the server stopped answering";
        LOG_ERROR("Lost the server: nothing heard for %.0f seconds", Network::Connection::TIMEOUT_SECONDS);
        return false;
    }

    // Once a frame, not per snapshot: the cap is on a frame's file reads.
    buildSpawned(scene, resources);
    return true;
}

bool NetClient::onWelcome(Scene& scene, Network::BitReader& reader) {
    // The server says it again until a command shows it arrived: the first one seated this end.
    if (m_localPlayer != NO_PLAYER) return true;

    WelcomeBody welcome;
    welcome.read(reader);

    // Else this end would report itself playing with no entity to play.
    if (reader.failed() || welcome.player == NO_PLAYER) {
        m_core.lastError = "the server's welcome did not say who this end is";
        LOG_ERROR("Welcome ended early or named no player; not joining");
        return false;
    }
    // Whether the world it loaded is the one the server holds; if not, which to load comes next.
    const uint8_t generation = welcome.worldGeneration;
    m_core.worldGeneration = welcome.current ? generation : static_cast<uint8_t>(generation - 1);

    m_localPlayer = welcome.player;
    // The slot names an entity of the server's world: until this end has loaded it (completeWorld),
    // it drives nothing.
    if (!welcome.current) {
        LOG_INFO("Joined as player %u; loading the server's world", m_localPlayer);
        return true;
    }
    if (!claimLocalEntity(scene, welcome.slot)) return false;
    LOG_INFO("Joined as player %u, driving entity %u", m_localPlayer, welcome.slot);
    return true;
}

bool NetClient::onWorld(Network::BitReader& reader) {
    WorldBody world;
    world.read(reader);
    if (reader.failed()) return true;
    if (world.generation == m_core.worldGeneration) return true;  // held already, or the news is late

    // Opened on the server's say-so: only a scene inside the project.
    if (!isProjectJsonPath(world.path)) {
        m_core.lastError = "the server named a world this end will not open";
        LOG_ERROR(
            "The server moved to '%s', which is no scene inside the project; leaving",
            world.path.c_str()
        );
        return false;
    }
    if (m_world.named && m_world.generation == world.generation) return true;
    m_world = WorldToLoad{
        std::move(world.path),
        world.fingerprint,
        world.slot,
        world.generation,
        true,
        false
    };
    LOG_INFO("The server moved to '%s'; loading it", m_world.path.c_str());
    return true;
}

std::string NetClient::takeSceneToLoad() {
    if (!m_world.named || m_world.taken) return {};
    m_world.taken = true;
    return m_world.path;
}

bool NetClient::completeWorld(Scene& scene) {
    const WorldToLoad world = m_world;
    m_world = WorldToLoad{};
    if (world.fingerprint != 0 && m_core.world != 0 && world.fingerprint != m_core.world) {
        m_core.lastError = "the world this end loaded is not the server's";
        LOG_ERROR(
            "Loaded '%s', but not as the server has it (%016llx here, %016llx there); leaving",
            world.path.c_str(),
            static_cast<unsigned long long>(m_core.world),
            static_cast<unsigned long long>(world.fingerprint)
        );
        return false;
    }
    if (!claimLocalEntity(scene, world.slot)) return false;
    m_core.worldGeneration = world.generation;
    LOG_INFO("Playing on in '%s', driving entity %u", world.path.c_str(), world.slot);
    return true;
}

bool NetClient::claimLocalEntity(Scene& scene, uint32_t slot) {
    m_localEntity = m_names.claim(scene, slot);
    if (m_localEntity) return true;
    m_core.lastError = "the server named an entity this world cannot hold";
    LOG_ERROR(
        "The server named slot %u for this end; the wire may name up to %u",
        slot,
        SlotAllocator::MAX_CLAIMED_INDEX
    );
    return false;
}

bool NetClient::onRefuse(Network::BitReader& reader) {
    const auto reason = static_cast<NetRefusal>(reader.u8());
    m_core.lastError = toString(reason);
    LOG_ERROR("Refused: %s", m_core.lastError.c_str());
    if (reason == NetRefusal::Mismatch) {
        LOG_ERROR("This end replicates: %s", NetSchema::get().describe().c_str());
    } else if (reason == NetRefusal::TickRate) {
        LOG_ERROR(
            "This end ticks at %u a second; the project's tickRate must match the server's",
            m_core.tickRate
        );
    }
    return false;
}

bool NetClient::onGoodbye() {
    m_core.lastError = "the server closed the game";
    LOG_INFO("The server closed the game");
    return false;
}

void NetClient::holdPredicted(Scene& scene, std::vector<DrawnFrom>& takenFrom) {
    m_prediction.held.clear();
    const auto holdAside = [&](EntityId entity) {
        const Transform* at = scene.tryGet<Transform>(entity);
        if (!at) return;
        Held held;
        held.entity    = entity;
        held.transform = *at;
        if (const Rigidbody* body = scene.tryGet<Rigidbody>(entity)) {
            held.hasBody = true;
            held.body    = *body;
        }
        if (const CharacterController* controller = scene.tryGet<CharacterController>(entity)) {
            held.hasController = true;
            held.controller    = *controller;
        }
        m_prediction.held.push_back(held);
    };
    holdAside(m_localEntity);

    // Not a body taken this snapshot: restoring its interpolated pose would keep
    // the simulation a render delay behind.
    takenFrom.clear();
    for (const Lease& lease : m_prediction.leases) {
        const bool justTaken = std::find(
            m_prediction.acquired.begin(),
            m_prediction.acquired.end(),
            lease.entity
        ) != m_prediction.acquired.end();
        if (!justTaken) {
            holdAside(lease.entity);
        } else if (const Transform* at = scene.tryGet<Transform>(lease.entity)) {
            takenFrom.push_back({lease.entity, at->position + drawOffsetFor(lease.entity)});
        }
    }
}

bool NetClient::adoptServerTick(uint32_t serverTick) {
    // One impossible tick would head every track, and each true sample after
    // it would be dropped as older.
    if (m_core.serverTick != 0 && serverTick > m_core.serverTick + NET_MAX_TICK_JUMP) {
        m_connection->refuse();
        if (!m_warnedTickJump) {
            m_warnedTickJump = true;
            LOG_ERROR(
                "A snapshot claimed tick %u against %u; ignoring it and any like it",
                serverTick,
                m_core.serverTick
            );
        }
        return false;
    }
    m_core.serverTick = serverTick;
    m_warnedTickJump  = false;
    return true;
}

void NetClient::onSnapshot(Scene& scene, Network::BitReader& reader) {
    SnapshotHeader header;
    header.read(reader);
    // Of a world this end does not hold - the last, or the next before it is loaded.
    if (!reader.failed() && header.worldGeneration != m_core.worldGeneration) return;

    // A header cut short reads as zeros, and a server tick of zero switches
    // the jump guard off.
    if (reader.failed()) {
        m_connection->refuse();
        return;
    }
    if (!adoptServerTick(header.serverTick)) return;
    const float tickRate = static_cast<float>(m_core.tickRate);
    m_interpolation.heard(header.serverTick, tickRate);
    m_prediction.heard = header.heard;
    m_outbox.acknowledge(header.messages);
    m_prediction.pacing.report(header.lowWater, m_connection->roundTrip(), tickRate);

    std::vector<DrawnFrom> takenFrom;
    holdPredicted(scene, takenFrom);

    // Before reading: an absent component means "as last time", and last time
    // must be the server's word, not the smoothing's.
    m_interpolation.restoreConfirmed(scene);

    m_touched.clear();
    m_lost.clear();
    const bool read = readSnapshot(scene, NetSchema::get(), reader, m_names, &m_touched, &m_lost);

    // Gone even from a body that faulted later: the losses come first.
    for (const EntityId entity : m_lost) m_interpolation.forget(entity);

    if (!read) {
        // Un-acknowledged too: the sender never re-describes what it believes
        // this end holds, so a standing claim would freeze those bodies.
        m_connection->refuse();

        // Else the scene keeps half a snapshot and the prediction is gone.
        restoreHeld(scene);

        LOG_ERROR("A snapshot did not decode; waiting for the next one");
        return;
    }

    for (const EntityId entity : m_touched) {
        if (!scene.isAlive(entity)) continue;
        // The entity this end drives is predicted, not drawn late.
        if (entity == m_localEntity) continue;
        const Transform* at = scene.tryGet<Transform>(entity);
        if (!at) continue;
        m_interpolation.record(entity, header.serverTick, at->position, at->rotation);
    }

    // A body just taken keeps the server's word and eases across the handover.
    for (const DrawnFrom& taken : takenFrom) {
        const Transform* at = scene.tryGet<Transform>(taken.entity);
        if (!at) continue;
        correctDraw(taken.entity, taken.drawn, at->position);
    }
    m_prediction.acquired.clear();

    // After the body is read: the snapshot may have reported this end's entity gone.
    if (scene.has<Transform>(m_localEntity)) reconcile(scene, header.confirmedTick);
    else                                     restoreHeld(scene);
}

void NetClient::send() {
    if (!m_connection) return;

    std::vector<uint8_t>& payload = m_core.payload;
    Network::BitWriter writer(payload, Network::Connection::MAX_PAYLOAD);
    // Drawn for the first Hello, then the Challenge's token, proving this end hears.
    const NetMessage message = m_localPlayer == NO_PLAYER ? NetMessage::Hello : NetMessage::Command;
    writeMessageHeader(writer, message, m_token);
    if (m_localPlayer == NO_PLAYER) {
        // Repeated every send until answered, so a lost Hello or Challenge
        // needs no retry timer.
        HelloBody hello;
        hello.schema      = NetSchema::get().fingerprint();
        hello.actions     = static_cast<uint8_t>(m_core.actionCount);
        hello.actionNames = m_core.actionNames;
        hello.world       = m_core.world;
        hello.tickRate    = static_cast<uint16_t>(m_core.tickRate);
        hello.write(writer);
    } else {
        // Before the commands, so it reads without any.
        CommandPreamble preamble;
        preamble.worldGeneration = m_core.worldGeneration;
        preamble.drawnTick       = m_interpolation.renderTick();
        preamble.write(writer);

        const size_t first = firstCommandToSend(
            m_prediction.unacknowledged,
            m_prediction.heard,
            m_prediction.sent
        );
        const size_t written = writeCommands(writer, m_prediction.unacknowledged, first, m_core.actionCount);
        if (written > 0) {
            m_prediction.sent = std::max(
                m_prediction.sent,
                m_prediction.unacknowledged[first + written - 1].sequence
            );
        }

        // In the room the commands left: they are for this tick, the messages for whenever.
        m_outbox.write(writer, 8u * Network::Connection::MAX_PAYLOAD - writer.bitCount());
    }
    writer.finish();

    m_connection->frame(payload.data(), payload.size(), m_core.datagram);
    m_core.socket.send(m_connection->peer(), m_core.datagram.data(), m_core.datagram.size());
}

void NetClient::buildSpawned(Scene& scene, ResourceManager& resources) {
    // Collected first: a build changes what the walk is over.
    std::vector<EntityId> waiting;
    scene.forEach<NetSpawn>([&](EntityId root, const NetSpawn&) { waiting.push_back(root); });
    if (waiting.empty()) return;
    std::sort(waiting.begin(), waiting.end(), [](EntityId a, EntityId b) { return a.slot() < b.slot(); });

    uint32_t built = 0;
    for (EntityId root : waiting) {
        if (built == NET_SPAWN_BUILDS_PER_FRAME) break;

        const NetSpawn spawn = scene.get<NetSpawn>(root);
        scene.remove<NetSpawn>(root);

        // What the decoder refused arrives with no path.
        if (spawn.prefab.empty()) {
            LOG_ERROR(
                "The server spawned something this end cannot read (slot %u here); nothing is built there",
                root.slot()
            );
            continue;
        }
        // Already built; said again because the confirmation was lost.
        if (const PrefabInstance* instance = scene.tryGet<PrefabInstance>(root)) {
            if (instance->source != spawn.prefab) {
                LOG_ERROR(
                    "The server spawned '%s' where this end holds '%s' (slot %u here); it is left as it is",
                    spawn.prefab.c_str(),
                    instance->source.c_str(),
                    root.slot()
                );
            }
            continue;
        }
        // Part of an instance built without named slots (too large to name).
        if (scene.has<PrefabEntity>(root)) {
            LOG_ERROR(
                "The server spawned '%s' where this end holds part of an instance (slot %u here) "
                    "it built; nothing is built over it",
                spawn.prefab.c_str(),
                root.slot()
            );
            continue;
        }

        buildInto(scene, resources, root, spawn);
        ++built;
    }
}

void NetClient::buildInto(Scene& scene, ResourceManager& resources, EntityId root, const NetSpawn& spawn) {
    // Set aside what snapshots said of this root: the prefab would overwrite
    // it, and the server, holding it confirmed, would never say it again.
    const NetSchema& schema = NetSchema::get();
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> said;
    for (uint32_t type = 0; type < schema.size(); ++type) {
        const NetType& row = schema.types()[type];
        if (!row.has(scene, root)) continue;
        std::vector<uint8_t> bytes;
        Network::BitWriter writer(bytes, Network::UdpSocket::MAX_DATAGRAM);
        row.encode(scene, root, writer);
        writer.finish();
        said.emplace_back(type, std::move(bytes));
    }

    // A root the wire never placed - a static body - stands where it was built.
    if (!scene.has<Transform>(root)) scene.add(root, Transform{spawn.at});
    scene.add(root, PrefabInstance{spawn.prefab, {}});

    const Prefab::BuiltSlots* slots = spawn.slots.empty() ? nullptr : &spawn.slots;
    if (!Prefab::instantiateInto(scene, resources, spawn.prefab, root, {}, nullptr, slots)) {
        // The root stays: the server still holds an entity at this slot.
        LOG_ERROR("The server built '%s' and this end could not", spawn.prefab.c_str());
        scene.remove<PrefabInstance>(root);
    }

    for (const auto& [type, bytes] : said) {
        Network::BitReader reader(bytes.data(), bytes.size());
        schema.types()[type].decode(scene, root, reader);
    }

    // A slot was already held here, so the ends' slots now differ.
    if (slots && Prefab::builtSlotsOf(scene, root) != spawn.slots) {
        LOG_WARNING(
            "'%s' (slot %u here) was not built into every slot the server used; a later spawn may "
                "find one taken",
            spawn.prefab.c_str(),
            root.slot()
        );
    }
}

std::string NetClient::describe() const {
    char line[320];
    if (!isPlaying()) {
        const std::string server = m_connection ? m_connection->peer().toString() : std::string{"nowhere"};
        std::snprintf(line, sizeof(line), "joining %s", server.c_str());
        return line;
    }
    std::snprintf(
        line,
        sizeof(line),
        "player %u, ping %.0f ms (+/- %.0f), losing %.1f%%, prediction out "
            "by %.0f mm, drawing %.0f ticks behind, smoothing %zu entities, "
            "server queue low %.1f aiming at %.1f, ticking %+.1f%%",
        m_localPlayer,
        roundTrip() * 1000.0f,
        m_connection ? m_connection->roundTripVariation() * 1000.0f : 0.0f,
        m_connection ? m_connection->lossFraction() * 100.0f : 0.0f,
        m_prediction.error * 1000.0f,
        renderDelay(),
        m_interpolation.tracked(),
        m_prediction.pacing.depth(),
        m_prediction.pacing.target(),
        (pacing() - 1.0f) * 100.0f
    );
    return line;
}

} // namespace Vkm::Engine
