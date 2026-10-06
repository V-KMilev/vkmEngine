#include "net/prediction/interpolation.h"

#include <algorithm>
#include <cmath>

#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "net/replication/silence.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief How far the render clock may drift before it is moved outright rather than eased.
 *
 * A gap this large is a stall or a reconnection.
 */
constexpr double SNAP_TICKS = 20.0;

/**
 * @brief How much a tick of error bends the rate the render clock runs at.
 *
 * Moving it would jump every body at once; nudging it toward a receding target
 * never arrives.
 */
constexpr float CATCH_UP = 0.05f;

/**
 * @brief The most the world may be shown speeded up or slowed down while the clock
 * catches up.
 *
 * Under what a player notices on a moving body.
 */
constexpr float MAX_DILATION = 0.10f;

/**
 * @brief How much of each arrival the measured jitter takes.
 *
 * One late packet moves it a little; a link that keeps jittering, all the way.
 */
constexpr float JITTER_SMOOTHING = 0.1f;

/**
 * @brief How many times the measured jitter the delay allows a snapshot to be late by.
 *
 * An arrival rarely strays further than twice the mean from when it was due.
 */
constexpr float JITTER_MARGIN = 2.0f;

} // namespace

void NetInterpolation::record(
    EntityId entity,
    uint32_t tick,
    const glm::vec3& position,
    const glm::quat& rotation
) {
    Track& track = m_tracks[entity.slot()];

    // A slot outlives its occupants; a stale track would slide the new one in
    // from where its predecessor died.
    if (track.entity != entity) track = Track{entity, {}, false};

    track.history.record(tick, position, rotation);

    if (!m_started || tick > m_newestTick) {
        m_newestTick = tick;
        m_started    = true;
    }
}

void NetInterpolation::heard(uint32_t tick, float tickRate) {
    // Against the last arrival, not a running estimate: a lost snapshot costs
    // nothing, and the two clocks' drift cancels between neighbours.
    if (m_heardAny && tickRate > 0.0f) {
        const double due      = static_cast<double>(static_cast<int64_t>(tick) - m_heardTick)
            / static_cast<double>(tickRate);
        const float  strayed  = static_cast<float>(std::abs((m_seconds - m_heardAt) - due));
        m_jitter += (strayed - m_jitter) * JITTER_SMOOTHING;
    }
    m_heardAt   = m_seconds;
    m_heardTick = tick;
    m_heardAny  = true;
}

float NetInterpolation::delayTicks(float tickRate, float snapshotRate) const {
    // One interval to the next snapshot, plus how late it may be; the floor
    // covers a lost one on a steady link.
    const float riding    = 1.0f + JITTER_MARGIN * m_jitter * snapshotRate;
    const float snapshots = std::min(std::max(SNAPSHOTS_BEHIND, riding), MAX_SNAPSHOTS_BEHIND);
    return snapshots * (tickRate / snapshotRate);
}

void NetInterpolation::restoreConfirmed(Scene& scene) {
    markPosedByAnimation(scene, m_posed);
    for (const auto& [slot, track] : m_tracks) {
        if (track.history.count == 0 || !scene.isAliveAtIndex(slot)) continue;
        const EntityId entity = scene.entityAt(slot);
        if (entity != track.entity) continue;
        Transform* held = scene.tryGet<Transform>(entity);
        if (!held || isPosed(slot)) continue;

        Transform& transform = *held;
        transform.position = track.history.samples[track.history.count - 1].position;
        transform.rotation = track.history.samples[track.history.count - 1].rotation;
    }
}

void NetInterpolation::apply(Scene& scene, float deltaTime, float tickRate, float snapshotRate) {
    m_seconds += static_cast<double>(deltaTime);
    if (!m_started) return;

    // Advanced by real time, not set from a snapshot, so the world keeps
    // moving between packets.
    const double target = static_cast<double>(m_newestTick) - delayTicks(tickRate, snapshotRate);
    const double error  = target - m_renderTick;

    if (std::abs(error) > SNAP_TICKS) {
        m_renderTick = target;
    } else {
        const float bend = static_cast<float>(error) * CATCH_UP;
        const float rate = 1.0f + std::max(-MAX_DILATION, std::min(MAX_DILATION, bend));
        m_renderTick += static_cast<double>(deltaTime * tickRate * rate);
    }

    // Never past the newest heard, or resumed news would snap every body; a
    // stall shows as a pause.
    m_renderTick = std::min(m_renderTick, static_cast<double>(m_newestTick));

    markPosedByAnimation(scene, m_posed);
    for (auto& [slot, track] : m_tracks) {
        if (track.held || !scene.isAliveAtIndex(slot)) continue;
        const EntityId entity = scene.entityAt(slot);
        if (entity != track.entity || !scene.has<Transform>(entity)) continue;

        // As the sender does: a bone the animation places would flicker
        // between two writers. The one on the current tick wins.
        if (isPosed(slot)) continue;

        Transform& transform = scene.get<Transform>(entity);
        track.history.poseAt(m_renderTick, transform.position, transform.rotation);
    }
}

void NetInterpolation::hold(EntityId entity, bool held) {
    Track& track = m_tracks[entity.slot()];
    if (track.entity != entity) track = Track{entity, {}, false};
    track.held = held;
}

void NetInterpolation::forget(EntityId entity) {
    m_tracks.erase(entity.slot());
}

void NetInterpolation::clear() {
    m_tracks.clear();
    m_renderTick = 0.0;
    m_newestTick = 0;
    m_started    = false;
    m_seconds    = 0.0;
    m_heardAt    = 0.0;
    m_heardTick  = 0;
    m_jitter     = 0.0f;
    m_heardAny   = false;
}

} // namespace Vkm::Engine
