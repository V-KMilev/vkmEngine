#pragma once

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
 *   AnimationManager::get().update(scene, deltaTime);
 */
class AnimationManager {
    public:
        AnimationManager(const AnimationManager& other) = delete;
        AnimationManager& operator=(const AnimationManager& other) = delete;

        AnimationManager(AnimationManager && other) = delete;
        AnimationManager& operator=(AnimationManager && other) = delete;

    public:
        /**
         * @brief Get the singleton instance.
         * @return Reference to the AnimationManager.
         */
        static AnimationManager& get();

        /**
         * @brief Update all animations in the scene.
         * @param scene The scene containing entities with Animation components.
         * @param deltaTime Time elapsed since last frame in seconds.
         */
         void update(Scene& scene, float deltaTime);

    private:
        AnimationManager() = default;
        ~AnimationManager() = default;

        /**
         * @brief Apply animation values to a Transform component.
         * @param animation The animation component.
         * @param transform The transform component to update.
         */
         void applyAnimation(const Animation& animation, Transform& transform) const;
};

} // namespace Engine
