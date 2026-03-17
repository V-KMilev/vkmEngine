#include "system/animation/animation_system.h"

#include <algorithm>
#include <cmath>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/component/animation.h"
#include "ecs/component/transform.h"
#include "system/visibility/visibility.h"
#include "platform/threading/thread_pool.h"

namespace Engine {

AnimationSystem::AnimationSystem() = default;

void AnimationSystem::update(FrameContext& ctx) {
    auto& scene = ctx.scene;
    const auto& visibility = *ctx.visibility;
    float deltaTime = ctx.deltaTime;

    // Phase 1: Update animation time for ALL animations (even culled ones)
    // This keeps animations synchronized even when entities go off-screen
    auto* animStorage = scene.storage<Animation>();
    if (!animStorage) return;

    const size_t animCount = animStorage->size();
    const size_t grain = std::max<size_t>(128, animCount / (ThreadPool::get().threadCount() * 4));

    parallelFor(animCount, grain, [&](size_t i) {
        Animation& animation = animStorage->dataAt(static_cast<uint32_t>(i));
        if (!animation.playing) return;

        animation.time += deltaTime * animation.speed;

        if (animation.duration > 0.0f && animation.time >= animation.duration) {
            if (animation.looping) {
                animation.time = std::fmod(animation.time, animation.duration);
            } else {
                animation.time = animation.duration;
                animation.playing = false;
            }
        }
    });

    // Phase 2: Apply animations only to visible entities
    const size_t visibleCount = visibility.entries.size();
    const size_t applyGrain = std::max<size_t>(64, visibleCount / (ThreadPool::get().threadCount() * 4));

    parallelFor(visibleCount, applyGrain, [&](size_t i) {
        const auto& entry = visibility.entries[i];
        if (!scene.isAlive(entry.id)) return;
        if (!scene.has<Animation>(entry.id)) return;
        if (!scene.has<Transform>(entry.id)) return;

        auto& animation = scene.get<Animation>(entry.id);
        if (!animation.playing) return;

        auto& transform = scene.get<Transform>(entry.id);
        applyAnimation(animation, transform);
    });
}

void AnimationSystem::applyAnimation(const Animation& animation, Transform& transform) const {
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
