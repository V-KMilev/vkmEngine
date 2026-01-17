#include "animation_manager.h"

#include <algorithm>

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
    auto& animationStorage = scene.storage<Animation>();
    auto& transformStorage = scene.storage<Transform>();

    // Update all animations (time progresses even for culled entities)
    for (EntityId id = 0; id < animationStorage.size(); ++id) {
        if (!animationStorage.has(id)) continue;

        auto& animation = animationStorage.get(id);

        if (!animation.playing) continue;

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

        // Only apply animation to transforms for visible entities
        // visibility.entities is sorted, so use binary_search for O(log n) lookup
        if (std::binary_search(visibility.entities.begin(), visibility.entities.end(), id)) {
            if (!transformStorage.has(id)) continue;

            auto& transform = transformStorage.get(id);
            applyAnimation(animation, transform);
        }
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

