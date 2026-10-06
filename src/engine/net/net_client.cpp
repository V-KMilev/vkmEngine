#define VKM_LOG_CATEGORY "NET"

#include "net/net_client.h"

#include <algorithm>
#include <cstdio>
#include <type_traits>
#include <utility>

#include <glm/gtc/constants.hpp>

#include "logger.h"

#include "core/memory/slot_allocator.h"
#include "ecs/component/prefab/net_spawn.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"
#include "net/net_session.h"
#include "net/wire/codecs.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Predicted positions kept, in ticks.
 *
 * Longer than NET_COMMAND_MEMORY, a replay's reach, so an answer too old to
 * replay is still measured and reported as the link outrunning the replay,
 * rather than going uncorrected in silence.
 */
constexpr size_t PREDICTION_HISTORY = 200;
static_assert(
    PREDICTION_HISTORY > NET_COMMAND_MEMORY,
    "an answer older than a replay can reach has to be recognised as one"
);

static_assert(
    std::is_same_v<decltype(NetSpawn::slots), Prefab::BuiltSlots>,
    "NetSpawn carries the prefab's own record of where an instance was built"
);

/**
 * @brief How long a lease outlives the last tick it was reported on.
 *
 * Must outlive a bounce and not the push.
 */
constexpr uint32_t LEASE_TICKS = 32;

/**
 * @brief Below this a disagreement is quantisation plus float noise, not worth a replay.
 */
constexpr float CORRECTION_FLOOR = 0.003f;

/**
 * @brief How fast what is drawn catches up with what is simulated, per second.
 *
 * Slow enough not to read as a jump, fast enough that nobody shoots where the
 * character is not.
 */
constexpr float DRAW_CATCH_UP = 6.0f;

/**
 * @brief Past this the error is a teleport or a respawn, drawn at once rather than eased.
 */
constexpr float DRAW_SNAP = 4.0f;

} // namespace

NetClient::NetClient(NetCore& core)
    : m_core(core)
{
}

NetClient::~NetClient() = default;

void NetClient::open(const NetAddress& server) {
    m_connection = std::make_unique<NetConnection>();
    m_connection->open(server);
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
}

void NetClient::advance(float seconds) {
    if (m_connection) m_connection->advance(seconds);
}

bool NetClient::simulates(EntityId entity) const {
    if (!entity) return false;
    if (entity == m_localEntity) return true;

    for (const Lease& lease : m_prediction.leases) {
        if (lease.entity == entity) return true;
    }
    return false;
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
}

void NetClient::lease(const std::vector<EntityId>& entities) {
    for (EntityId entity : entities) {
        if (!entity || entity == m_localEntity) continue;

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
        if (!held.hasBody) continue;
        if (Rigidbody* body = scene.tryGet<Rigidbody>(held.entity)) *body = held.body;
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

    m_interpolation.apply(scene, deltaTime, static_cast<float>(m_core.tickRate), NET_SNAPSHOT_RATE);

    for (size_t i = 0; i < m_prediction.released.size(); ++i) {
        const EntityId entity = m_prediction.released[i];
        const Transform* at = scene.tryGet<Transform>(entity);
        if (!at) continue;
        correctDraw(entity, before[i], at->position);
    }
    m_prediction.released.clear();
}

bool NetClient::receive(Scene& scene, ResourceManager& resources) {
    NetAddress from;
    // Bounded rather than drained - see NetCore::MAX_READS_PER_FRAME.
    for (size_t read = 0; read < NetCore::MAX_READS_PER_FRAME; ++read) {
        if (!m_core.socket.receive(m_core.datagram, from)) break;
        if (!m_connection || !(from == m_connection->peer())) continue;

        // Checked before the connection sees it: a server address is as easy to forge.
        BitReader       reader  = receivedPayload(m_core);
        NetMessage      message = NetMessage::Hello;
        uint64_t        token   = 0;
        const NetSender sender  = readMessageHeader(reader, message, token);
        if (sender == NetSender::Unknown) continue;

        // All another build can say that this end understands is a refusal.
        if (sender == NetSender::OtherBuild) {
            if (message != NetMessage::Refuse || m_localPlayer != NO_PLAYER || token != m_token) {
                continue;
            }
            return onRefuse(reader);
        }

        // The token Hellos echo. The connection does not see it, since nothing
        // proves the sender; a forged one costs one Hello and a fresh Challenge.
        if (message == NetMessage::Challenge) {
            if (m_localPlayer == NO_PLAYER) m_token = token;
            continue;
        }
        if (token != m_token) continue;

        m_core.acknowledged.clear();
        const bool accepted = m_connection->accept(
            m_core.datagram.data(),
            m_core.datagram.size(),
            m_core.scratch,
            m_core.acknowledged
        );
        if (!accepted) continue;

        bool open = true;
        switch (message) {
            case NetMessage::Welcome:  open = onWelcome(scene, reader); break;
            case NetMessage::Refuse:   open = onRefuse(reader);         break;
            case NetMessage::Goodbye:  open = onGoodbye();              break;
            case NetMessage::Snapshot: onSnapshot(scene, reader);       break;
            default: break;
        }
        if (!open) return false;
    }

    if (m_connection && m_connection->timedOut()) {
        m_core.lastError = "the server stopped answering";
        LOG_ERROR("Lost the server: nothing heard for %.0f seconds", NetConnection::TIMEOUT_SECONDS);
        return false;
    }

    // Once a frame, not per snapshot: the cap is on a frame's file reads.
    buildSpawned(scene, resources);
    return true;
}

bool NetClient::onWelcome(Scene& scene, BitReader& reader) {
    const PlayerId player = static_cast<PlayerId>(reader.u16());
    const uint32_t slot   = reader.u32();

    // Else this end would report itself playing with no entity to play.
    if (reader.failed() || player == NO_PLAYER) {
        m_core.lastError = "the server's welcome did not say who this end is";
        LOG_ERROR("Welcome ended early or named no player; not joining");
        return false;
    }

    m_localPlayer = player;
    m_localEntity = scene.isAliveAtIndex(slot) ? scene.entityAt(slot) : scene.createEntityAt(slot);
    if (!m_localEntity) {
        m_core.lastError = "the server named an entity this world cannot hold";
        LOG_ERROR(
            "Welcome named slot %u; the wire may name up to %u",
            slot,
            SlotAllocator::MAX_CLAIMED_INDEX
        );
        return false;
    }
    LOG_INFO("Joined as player %u, driving entity %u", m_localPlayer, slot);
    return true;
}

bool NetClient::onRefuse(BitReader& reader) {
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

void NetClient::onSnapshot(Scene& scene, BitReader& reader) {
    const uint32_t serverTick    = reader.u32();
    const uint32_t confirmedTick = reader.u32();
    const uint32_t heard         = reader.u32();
    const uint32_t lowWater      = reader.bits(NET_QUEUE_DEPTH_BITS);

    // A header cut short reads as zeros, and a server tick of zero switches
    // the jump guard off.
    if (reader.failed()) {
        m_connection->refuse();
        return;
    }
    if (!adoptServerTick(serverTick)) return;
    m_interpolation.heard(serverTick, static_cast<float>(m_core.tickRate));
    m_prediction.heard = heard;
    m_prediction.pacing.report(lowWater, m_connection->roundTrip(), static_cast<float>(m_core.tickRate));

    std::vector<DrawnFrom> takenFrom;
    holdPredicted(scene, takenFrom);

    // Before reading: an absent component means "as last time", and last time
    // must be the server's word, not the smoothing's.
    m_interpolation.restoreConfirmed(scene);

    m_touched.clear();
    if (!readSnapshot(scene, NetSchema::get(), reader, &m_touched)) {
        // Un-acknowledged too: the sender never re-describes what it believes
        // this end holds, so a standing claim would freeze those bodies.
        m_connection->refuse();

        // Else the scene keeps half a snapshot and the prediction is gone.
        restoreHeld(scene);

        LOG_ERROR("A snapshot did not decode; waiting for the next one");
        return;
    }

    for (uint32_t slot : m_touched) {
        if (!scene.isAliveAtIndex(slot)) continue;
        const EntityId entity = scene.entityAt(slot);
        // The entity this end drives is predicted, not drawn late.
        if (entity == m_localEntity) continue;
        const Transform* at = scene.tryGet<Transform>(entity);
        if (!at) continue;
        m_interpolation.record(entity, serverTick, at->position, at->rotation);
    }

    // A body just taken keeps the server's word and eases across the handover.
    for (const DrawnFrom& taken : takenFrom) {
        const Transform* at = scene.tryGet<Transform>(taken.entity);
        if (!at) continue;
        correctDraw(taken.entity, taken.drawn, at->position);
    }
    m_prediction.acquired.clear();

    // After the body is read: the snapshot may have reported this end's entity gone.
    if (scene.has<Transform>(m_localEntity)) reconcile(scene, confirmedTick);
    else                                     restoreHeld(scene);
}

void NetClient::send() {
    if (!m_connection) return;

    // Not before the first tick: a behavior sets the action count the hello carries.
    if (!m_core.ticked) return;

    std::vector<uint8_t>& payload = m_core.payload;
    BitWriter writer(payload, NetConnection::MAX_PAYLOAD);
    // Zero on the first Hello, then the Challenge's token, proving this end hears.
    const NetMessage message = m_localPlayer == NO_PLAYER ? NetMessage::Hello : NetMessage::Command;
    writeMessageHeader(writer, message, m_token);
    if (m_localPlayer == NO_PLAYER) {
        // Repeated every send until answered, so a lost Hello or Challenge
        // needs no retry timer.
        writer.u32(NetSchema::get().fingerprint());
        writer.u8(static_cast<uint8_t>(m_core.actionCount));
        writer.u64(m_core.actionNames);
        writer.u64(m_core.world);
        writer.u16(static_cast<uint16_t>(m_core.tickRate));
    } else {
        // The server tick being drawn, before the commands so it reads without any.
        const double   drawn = m_interpolation.renderTick();
        const uint32_t whole = drawn > 0.0 ? static_cast<uint32_t>(drawn) : 0u;
        writer.u32(whole);
        writer.bits(static_cast<uint32_t>((drawn - static_cast<double>(whole)) * 255.0), 8);

        const size_t first   = firstCommandToSend(
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
                "The server spawned something at slot %u that this end cannot read; nothing is built there",
                root.slot()
            );
            continue;
        }
        // Already built; said again because the confirmation was lost.
        if (const PrefabInstance* instance = scene.tryGet<PrefabInstance>(root)) {
            if (instance->source != spawn.prefab) {
                LOG_ERROR(
                    "The server spawned '%s' at slot %u, which this end holds as '%s'; it is left as it is",
                    spawn.prefab.c_str(),
                    root.slot(),
                    instance->source.c_str()
                );
            }
            continue;
        }
        // Part of an instance built without named slots (too large to name).
        if (scene.has<PrefabEntity>(root)) {
            LOG_ERROR(
                "The server spawned '%s' at slot %u, which holds part of an instance "
                    "this end built; nothing is built over it",
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
        BitWriter writer(bytes, UdpSocket::MAX_DATAGRAM);
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
        BitReader reader(bytes.data(), bytes.size());
        schema.types()[type].decode(scene, root, reader);
    }

    // A slot was already held here, so the ends' slots now differ.
    if (slots && Prefab::builtSlotsOf(scene, root) != spawn.slots) {
        LOG_WARNING(
            "'%s' at slot %u was not built into every slot the server used; a later spawn may "
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
