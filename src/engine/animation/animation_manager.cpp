#include "animation_manager.h"

#include <algorithm>

#include "logger.h"

#include "scene.h"
#include "animation.h"
#include "transform.h"
#include "scene_view.h"

namespace Engine {

void AnimationManager::update(
    float deltaTime,
    Scene& scene,
    const std::vector<uint32_t>& visibleIds
) {
    auto& animationStorage = scene.storage<Animation>();
    auto& transformStorage = scene.storage<Transform>();

    // Update animation time for ALL animations (even culled ones)
    // This keeps animations synchronized even when entities go off-screen
    for (EntityId id = 0; id < animationStorage.size(); ++id) {
        if (!animationStorage.has(id)) continue;

        auto& animation = animationStorage.get(id);

        if (!animation.playing) continue;

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
    }

    // Apply animations only to visible entities
    // Iterate visibility list instead of all animations (much faster when few visible)
    for (EntityId id : visibleIds) {
        if (!animationStorage.has(id)) continue;
        if (!transformStorage.has(id)) continue;

        auto& animation = animationStorage.get(id);
        if (!animation.playing) continue;

        auto& transform = transformStorage.get(id);
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

