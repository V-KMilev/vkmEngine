#include "animation_manager.h"

#include <algorithm>
#include <cmath>

#include "logger.h"

#include "scene.h"
#include "animation.h"
#include "transform.h"

namespace Engine {

AnimationManager& AnimationManager::get() {
    static AnimationManager s_instance;
    return s_instance;
}

void AnimationManager::update(Scene& scene, float deltaTime) {
    auto& animationStorage = scene.storage<Animation>();
    auto& transformStorage = scene.storage<Transform>();

    // Iterate over all animation components
    for (EntityId id = 0; id < animationStorage.size(); ++id) {
        if (!animationStorage.has(id)) {
            continue;
        }

        auto& animation = animationStorage.get(id);

        // Skip if not playing
        if (!animation.playing) {
            continue;
        }

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

        // Apply animation to transform if it exists
        if (transformStorage.has(id)) {
            auto& transform = transformStorage.get(id);
            applyAnimation(animation, transform);
        }
    }
}

void AnimationManager::applyAnimation(const Animation& animation, Transform& transform) const {
    float time = animation.time;

    // Apply position animation
    if (!animation.positionTrack.isEmpty()) {
        transform.position = animation.positionTrack.getValue(time);
    }

    // Apply rotation animation
    if (!animation.rotationTrack.isEmpty()) {
        transform.rotation = animation.rotationTrack.getValue(time);
    }

    // Apply scale animation
    if (!animation.scaleTrack.isEmpty()) {
        transform.scale = animation.scaleTrack.getValue(time);
    }
}

} // namespace Engine

