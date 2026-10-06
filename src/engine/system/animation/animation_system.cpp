#include "system/animation/animation_system.h"

#include <algorithm>

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/core/transform.h"
#include "platform/threading/thread_pool.h"
#include "system/animation/pose_evaluator.h"

namespace Vkm::Engine {

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

    const size_t grain = std::max<size_t>(128, animCount / (ThreadPool::get().threadCount() * 4));

    // Each iteration touches a distinct Transform slot, and no storage is added or removed meanwhile.
    parallelFor(animCount, grain, [&](size_t i) {
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
}

void AnimationSystem::applyAnimation(const Animation& animation, Transform& transform) {
    float time = animation.time;

    if (!animation.positionTrack.isEmpty()) {
        transform.position = animation.positionTrack.getValue(time);
    }
    if (!animation.rotationTrack.isEmpty()) {
        transform.rotation = animation.rotationTrack.getValue(time);
    }
    if (!animation.scaleTrack.isEmpty()) {
        transform.scale = animation.scaleTrack.getValue(time);
    }
}

} // namespace Vkm::Engine
