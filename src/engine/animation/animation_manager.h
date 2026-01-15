#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

#include "entity.h"

namespace Engine {
    class Scene;
    struct Animation;
    struct Transform;
}

namespace Engine {

/**
 * @brief System that processes and updates all Animation components in the scene.
 *
 * The AnimationManager:
 * - Updates animation timelines based on delta time
 * - Applies animated values to Transform components
 * - Handles playback control (play, pause, loop, speed)
 *
 * Usage:
 * @code
 *   AnimationManager animationManager;
 *   animationManager.update(deltaTime, scene, visibleIds);
 * @endcode
 */
class AnimationManager {
public:
    AnimationManager() = default;
    ~AnimationManager() = default;

    AnimationManager(const AnimationManager& other) = delete;
    AnimationManager& operator=(const AnimationManager& other) = delete;

    AnimationManager(AnimationManager && other) = delete;
    AnimationManager& operator=(AnimationManager && other) = delete;

public:
    /**
     * @brief Update only animations for visible entities.
     *
     * This is the primary update method - only animates entities that are
     * currently visible in the camera frustum, saving CPU time.
     *
     * @param scene The scene containing entities with Animation components.
     * @param deltaTime Time elapsed since last frame in seconds.
     * @param visibleIds List of entity IDs that are visible (from BVH query).
     */
    void update(float deltaTime, Scene& scene, const std::vector<EntityId>& visibleIds);

private:
    /**
     * @brief Update a single animation and apply it to its transform.
     */
    void updateAnimation(Animation& animation, Transform& transform, float deltaTime);
};

} // namespace Engine
