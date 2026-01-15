#include "animation_manager.h"

#include <algorithm>
#include <cmath>

#include "scene.h"
#include "animation.h"
#include "transform.h"

namespace Engine {

void AnimationManager::update(float deltaTime, Scene& scene, const std::vector<uint32_t>& visibleIds) {
    auto& animationStorage = scene.storage<Animation>();
    auto& transformStorage = scene.storage<Transform>();

    // Only update animations for visible entities
    for (uint32_t id : visibleIds) {
        if (!animationStorage.has(id)) continue;
        if (!transformStorage.has(id)) continue;

        auto& animation = animationStorage.get(id);
        if (!animation.playing) continue;

        auto& transform = transformStorage.get(id);

        updateAnimation(animation, transform, deltaTime);
    }
}

void AnimationManager::updateAnimation(Animation& animation, Transform& transform, float deltaTime) {
    // Update animation time
    animation.time += deltaTime * animation.speed;

    // Calculate duration (longest track)
    float duration = std::max({
        animation.positionTrack.getDuration(),
        animation.rotationTrack.getDuration(),
        animation.scaleTrack.getDuration()
    });

    // Handle looping and end of animation
    if (duration > 0.0f && animation.time >= duration) {
        if (animation.looping) {
            animation.time = std::fmod(animation.time, duration);
        } else {
            animation.time = duration;
            animation.playing = false;
        }
    }

    // Apply animation values to transform
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

