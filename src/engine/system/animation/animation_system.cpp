#include "system/animation/animation_system.h"

#include <algorithm>
#include <cmath>

#include "core/clock.h"
#include "core/math/rotation.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "platform/threading/thread_pool.h"
#include "system/animation/pose_evaluator.h"

namespace Vkm::Engine {

namespace {

/// How far a playhead may land from where its step puts it before it counts as a jump: a lap
/// wrapped, a spent one-shot rewound.
constexpr float HEAD_JUMP = 1e-4f;

void poseAt(const Animation& animation, float time, Transform& transform) {
    if (!animation.positionTrack.isEmpty()) transform.position = animation.positionTrack.getValue(time);
    if (!animation.rotationTrack.isEmpty()) transform.rotation = animation.rotationTrack.getValue(time);
    if (!animation.scaleTrack.isEmpty()) transform.scale = animation.scaleTrack.getValue(time);
}

} // namespace

void AnimationSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("AnimationSystem");

    auto& scene = ctx.scene;
    // No pause test: a fixedUpdate runs only when simulation time elapsed, including the editor's
    // single step while paused.
    const float simDelta = ctx.clock.getFixedStep();

    auto* animStorage = scene.storage<Animation>();
    if (!animStorage) return;
    // May be null: an animation with nothing to pose still advances and finishes.
    auto* transforms = scene.storage<Transform>();

    const size_t animCount = animStorage->size();

    // The Kinematic bodies among them, few: their world poses taken around the pass, their chains
    // walked as physics walks them.
    m_driven.clear();
    const auto* bodies = scene.storage<Rigidbody>();
    for (uint32_t i = 0; bodies && transforms && i < animCount; ++i) {
        const uint32_t entityIdx = animStorage->keyAt(i);
        if (!bodies->contains(entityIdx) || !transforms->contains(entityIdx)) continue;
        if (bodies->get(entityIdx).motion != RigidbodyMotion::Kinematic) continue;
        const EntityId   id        = scene.entityAt(entityIdx);
        const Animation& animation = animStorage->dataAt(i);
        const bool       starting  = animation.playOnStart && !animation.started;
        const BodyPose   before    = worldPoseOf(scene, id, transforms->get(entityIdx));
        m_driven.push_back({id, i, starting, animation.time, before});
    }

    // Each iteration touches a distinct Transform slot, and no storage is added or removed meanwhile.
    parallelFor(animCount, [&](size_t i) {
        Animation& animation = animStorage->dataAt(static_cast<uint32_t>(i));

        // Here rather than at load, so an animation starts on Play and stays still in an open scene.
        if (animation.playOnStart && !animation.started) {
            animation.started = true;
            animation.playing = true;
        }
        if (!animation.playing) return;

        const float duration = Animation::computeDuration(animation);
        const float delta    = simDelta * animation.speed;
        rewindSpentHead(animation.time, duration, delta, animation.looping);
        if (!advanceHead(animation.time, duration, delta, animation.looping)) {
            animation.playing = false;
        }

        const uint32_t entityIdx = animStorage->keyAt(static_cast<uint32_t>(i));
        if (transforms && transforms->contains(entityIdx)) {
            applyAnimation(animation, transforms->get(entityIdx));
        }
    });

    // Moving at what the clip moved it, and still once the clip stops, run out or stopped by gameplay;
    // what the clip does not move (a channel with no track, a body with no clip) is gameplay's to drive.
    for (const Driven& driven : m_driven) {
        Rigidbody& body      = scene.get<Rigidbody>(driven.id);
        Animation& animation = animStorage->dataAt(driven.slot);
        const bool moves     = !animation.positionTrack.isEmpty();
        const bool turns     = !animation.rotationTrack.isEmpty();
        if (animation.playing && simDelta > 0.0f) {
            const Transform& now   = scene.get<Transform>(driven.id);
            const BodyPose   after = worldPoseOf(scene, driven.id, now);
            BodyPose         from  = driven.before;
            float            span  = simDelta;

            // Across a jump of the head, or on the tick playOnStart starts the clip (its body may stand
            // anywhere), the clip's own motion instead: its track a step back on this side of the jump.
            const float delta = simDelta * animation.speed;
            if (driven.starting || std::abs(animation.time - (driven.time + delta)) > HEAD_JUMP) {
                const float duration = Animation::computeDuration(animation);
                const float back     = glm::clamp(animation.time - delta, 0.0f, duration);
                Transform   prior    = now;
                poseAt(animation, back, prior);
                from = worldPoseOf(scene, driven.id, prior);
                span = animation.speed != 0.0f ? (animation.time - back) / animation.speed : 0.0f;
            }
            // No span at all (a clip at speed zero) moves it nowhere.
            const float rate = span > 0.0f ? 1.0f / span : 0.0f;
            if (moves) body.linearVelocity = (after.position - from.position) * rate;
            if (turns) body.angularVelocity = Math::rotationBetween(from.rotation, after.rotation) * rate;
            animation.movingBody = true;
        } else if (animation.movingBody) {
            if (moves) body.linearVelocity = glm::vec3(0.0f);
            if (turns) body.angularVelocity = glm::vec3(0.0f);
            animation.movingBody = false;
        }
    }
}

void AnimationSystem::applyAnimation(const Animation& animation, Transform& transform) {
    poseAt(animation, animation.time, transform);
}

} // namespace Vkm::Engine
