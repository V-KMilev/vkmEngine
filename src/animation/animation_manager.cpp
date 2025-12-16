#include "animation_manager.h"

#include "logger.h"

namespace Engine {

AnimationManager& AnimationManager::get() {
    static AnimationManager s_instance;
    return s_instance;
}

void AnimationManager::update(Scene& scene, float deltaTime) {
    for (auto& entity : scene.getEntities()) {
        // Find Animation component
        auto animation = Scene::findComponentAs<Animation>(entity, ComponentType::Animation);
        if (!animation) {
            continue;
        }

        // Update animation timeline
        animation->update(deltaTime);

        // Find Transform component to apply animation to
        auto transform = Scene::findComponentAs<Transform>(entity, ComponentType::Transform);
        if (!transform) {
            continue;
        }

        // Apply animation to the transform
        applyAnimation(*animation, *transform);
    }
}

void AnimationManager::applyAnimation(const Animation& animation, Transform& transform) const {
    float time = animation.getTime();

    // Apply position animation
    if (!animation.getPositionTrack().isEmpty()) {
        glm::vec3 position = animation.getPositionTrack().getValue(time);
        transform.setPosition(position);
    }

    // Apply rotation animation
    if (!animation.getRotationTrack().isEmpty()) {
        glm::quat rotation = animation.getRotationTrack().getValue(time);
        transform.setRotation(rotation);
    }

    // Apply scale animation
    if (!animation.getScaleTrack().isEmpty()) {
        glm::vec3 scale = animation.getScaleTrack().getValue(time);
        transform.setScale(scale);
    }
}

} // namespace Engine

