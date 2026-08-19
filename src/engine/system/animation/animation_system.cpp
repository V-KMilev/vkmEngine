#include "system/animation/animation_system.h"

#include <algorithm>
#include <cmath>

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation.h"
#include "ecs/component/transform.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

void AnimationSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("AnimationSystem");

    auto& scene = ctx.scene;
    const float simDelta = ctx.clock.getSimDelta();

    // No simulation time elapsed (paused, or not stepping this frame): advance
    // nothing and, crucially, apply nothing - so authoring a Transform while
    // paused is not overwritten by re-sampling the track at the same time.
    if (simDelta <= 0.0f) return;

    auto* animStorage = scene.storage<Animation>();
    if (!animStorage) return;

    const size_t animCount = animStorage->size();

    const size_t grain = std::max<size_t>(128, animCount / (ThreadPool::get().threadCount() * 4));

    // Safe across threads because each iteration touches a distinct entity's Transform
    // slot and no component types are being added/removed during the loop.
    parallelFor(animCount, grain, [&](size_t i) {
        Animation& animation = animStorage->dataAt(static_cast<uint32_t>(i));
        if (!animation.playing) return;

        animation.time += simDelta * animation.speed;

        if (animation.duration > 0.0f && animation.time >= animation.duration) {
            if (animation.looping) {
                animation.time = std::fmod(animation.time, animation.duration);
            } else {
                animation.time = animation.duration;
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

} // namespace Engine
