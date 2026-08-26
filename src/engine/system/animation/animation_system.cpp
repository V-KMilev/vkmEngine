#include "system/animation/animation_system.h"

#include <algorithm>
#include <cmath>

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/core/transform.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {

void AnimationSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("AnimationSystem");

    auto& scene = ctx.scene;
    // No pause test: reaching a fixedUpdate means a step was consumed, and one
    // is only consumed when simulation time elapsed - the editor's single step
    // is paused and stepping at once.
    const float simDelta = ctx.clock.getFixedStep();

    auto* animStorage = scene.storage<Animation>();
    if (!animStorage) return;

    const size_t animCount = animStorage->size();

    const size_t grain = std::max<size_t>(128, animCount / (ThreadPool::get().threadCount() * 4));

    // Safe across threads because each iteration touches a distinct entity's Transform
    // slot and no component types are being added/removed during the loop.
    parallelFor(animCount, grain, [&](size_t i) {
        Animation& animation = animStorage->dataAt(static_cast<uint32_t>(i));

        // The authored flag becomes the runtime one, once, and here rather than
        // at load: that is what makes an animation start on Play and stay still
        // in a scene that is only open.
        if (animation.playOnStart && !animation.started) {
            animation.started = true;
            animation.playing = true;
        }
        if (!animation.playing) return;

        animation.time += simDelta * animation.speed;

        const float duration = Animation::computeDuration(animation);
        if (duration > 0.0f && animation.time >= duration) {
            if (animation.looping) {
                animation.time = std::fmod(animation.time, duration);
            } else {
                animation.time = duration;
                animation.playing = false;
            }
        }

        const uint32_t entityIdx = animStorage->keyAt(static_cast<uint32_t>(i));
        const EntityId id = scene.entityAt(entityIdx);
        if (scene.has<Transform>(id)) applyAnimation(animation, scene.get<Transform>(id));
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
