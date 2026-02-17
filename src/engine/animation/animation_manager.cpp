#include "animation_manager.h"

#include <algorithm>
#include <cmath>

#include "logger.h"

#include "scene.h"
#include "animation.h"
#include "transform.h"
#include "visibility.h"

namespace Engine {

void AnimationManager::update(
    Scene& scene,
    const Visibility& visibility,
    float deltaTime
) {
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
    for (const EntityId& id : visibility.entities) {
        if (!scene.has<Animation>(id)) continue;
        if (!scene.has<Transform>(id)) continue;

        auto& animation = scene.get<Animation>(id);
        if (!animation.playing) continue;

        auto& transform = scene.get<Transform>(id);
        applyAnimation(animation, transform);
    }
}

void AnimationManager::applyAnimation(const Animation& animation, Transform& transform) const {
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
