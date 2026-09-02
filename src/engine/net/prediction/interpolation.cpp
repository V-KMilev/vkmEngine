#include "net/prediction/interpolation.h"

#include <algorithm>

#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "net/replication/silence.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief How far the render clock may drift from where it should be before it is moved
 * there outright rather than eased.
 *
 * A gap this large is a stall or a reconnection, and easing across it would take longer
 * than it took to open.
 */
constexpr float SNAP_TICKS = 20.0f;

/**
 * @brief How much a tick of error bends the rate the render clock runs at.
 *
 * Bent rather than moved: moving it is a visible jump on every body at once, and a
 * clock nudged toward a target it advances away from never arrives.
 */
constexpr float CATCH_UP = 0.05f;

/**
 * @brief The most the world may be shown speeded up or slowed down while the clock
 * catches up.
 *
 * Ten per cent is under what a player notices on a moving body and closes a tick of
 * error in about twenty frames.
 */
constexpr float MAX_DILATION = 0.10f;

} // namespace

void NetInterpolation::record(EntityId entity, uint32_t tick,
                              const glm::vec3& position, const glm::quat& rotation) {
    Track& track = m_tracks[entity.slot()];

    // A slot outlives its occupants, and only one of the paths a destroyed
    // entity arrives by forgets anything. A track still naming the last
    // occupant slides the new one in from wherever its predecessor died.
    if (track.entity != entity) track = Track{entity, {}, false};

    track.history.record(tick, position, rotation);

    if (!m_started || tick > m_newestTick) {
        m_newestTick = tick;
        m_started    = true;
    }
}

void NetInterpolation::restoreConfirmed(Scene& scene) const {
    for (const auto& [slot, track] : m_tracks) {
        if (track.history.count == 0 || !scene.isAliveAtIndex(slot)) continue;
        const EntityId entity = scene.entityAt(slot);
        if (entity != track.entity || !scene.has<Transform>(entity)) continue;
        if (isPosedByAnimation(scene, entity)) continue;

        Transform& transform = scene.get<Transform>(entity);
        transform.position = track.history.samples[track.history.count - 1].position;
        transform.rotation = track.history.samples[track.history.count - 1].rotation;
    }
}

void NetInterpolation::apply(Scene& scene, float deltaTime, float tickRate, float snapshotRate) {
    if (!m_started) return;

    // The clock that decides what is drawn runs at the simulation's rate, a
    // fixed distance behind the newest news. Advanced by real time rather than
    // set from a snapshot, so the world keeps moving between packets.
    const float target = static_cast<float>(m_newestTick) - delayTicks(tickRate, snapshotRate);
    const float error  = target - m_renderTick;

    if (std::abs(error) > SNAP_TICKS) {
        m_renderTick = target;
    } else {
        const float rate = 1.0f + std::max(-MAX_DILATION, std::min(MAX_DILATION, error * CATCH_UP));
        m_renderTick += deltaTime * tickRate * rate;
    }

    // Never past the newest thing heard: beyond it there is nothing to
    // interpolate toward, and a clock that ran on would be ahead when news
    // resumed, snapping every body. Held here, a stall shows as a pause.
    m_renderTick = std::min(m_renderTick, static_cast<float>(m_newestTick));

    for (auto& [slot, track] : m_tracks) {
        if (track.held || !scene.isAliveAtIndex(slot)) continue;
        const EntityId entity = scene.entityAt(slot);
        if (entity != track.entity || !scene.has<Transform>(entity)) continue;

        // The same rule the sender applied. A bone the animation places has a
        // second writer this frame, and two taking turns is a pose that flickers
        // with however many fixed steps ran. The one on the current tick wins.
        if (isPosedByAnimation(scene, entity)) continue;

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
    m_renderTick = 0.0f;
    m_newestTick = 0;
    m_started    = false;
}

} // namespace Vkm::Engine
