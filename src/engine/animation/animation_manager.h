#pragma once

#include <vector>
#include <cstdint>

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
 *   animationManager.update(scene, deltaTime);
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
         * @brief Update all animations in the scene.
         * @param scene The scene containing entities to update.
         * @param visibility The visibility result containing visible entities.
         * @param deltaTime Time elapsed since last frame in seconds.
         */
         void update(
            float deltaTime,
            Scene& scene,
            const std::vector<uint32_t>& visibleIds
        );

    private:

        /**
         * @brief Apply animation values to a Transform component.
         * @param animation The animation component.
         * @param transform The transform component to update.
         */
         void applyAnimation(const Animation& animation, Transform& transform) const;
};

} // namespace Engine
