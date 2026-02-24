#include "system/animation/animation_system.h"

#include <algorithm>
#include <cmath>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/component/animation.h"
#include "ecs/component/transform.h"
#include "system/visibility/visibility.h"

namespace Engine {

AnimationSystem::AnimationSystem() = default;

void AnimationSystem::update(FrameContext& ctx) {
    auto& scene = ctx.scene;
    const auto& visibility = *ctx.visibility;
    float deltaTime = ctx.deltaTime;

    // Update animation time for ALL animations (even culled ones)
    // This keeps animations synchronized even when entities go off-screen
    scene.forEach<Animation>([&](EntityId id, Animation& animation) {
        if (!animation.playing) return;

        // Update animation time
        animation.time += deltaTime * animation.speed;

        // Handle looping and end of animation
        if (animation.duration > 0.0f && animation.time >= animation.duration) {
            if (animation.looping) {
                animation.time = std::fmod(animation.time, animation.duration);
            } else {
                animation.time = animation.duration;
                animation.playing = false;
            }
        }
    });

    // Apply animations only to visible entities
    for (const auto& entry : visibility.entries) {
        if (!scene.isAlive(entry.id)) continue;
        if (!scene.has<Animation>(entry.id)) continue;
        if (!scene.has<Transform>(entry.id)) continue;

        auto& animation = scene.get<Animation>(entry.id);
        if (!animation.playing) continue;

        auto& transform = scene.get<Transform>(entry.id);
        applyAnimation(animation, transform);
    }
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
