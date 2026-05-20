#pragma once

#include "core/system.h"

namespace Engine {

struct Animation;
struct Transform;

/**
 * @brief System that processes and updates all Animation components in the scene.
 * 
 * The AnimationSystem:
 * - Updates animation timelines based on delta time
 * - Applies animated values to Transform components
 * - Handles playback control (play, pause, loop, speed)
 * 
 * Usage:
 * @code
 *   AnimationSystem animationManager;
 *   animationManager.update(scene, deltaTime);
 * @endcode
 */
class AnimationSystem : public System {
    public:
        AnimationSystem();
        ~AnimationSystem() override = default;

        AnimationSystem(const AnimationSystem& other) = delete;
        AnimationSystem& operator=(const AnimationSystem& other) = delete;

        AnimationSystem(AnimationSystem && other) = delete;
        AnimationSystem& operator=(AnimationSystem && other) = delete;

    public:
        /**
         * @brief Update all animations in the scene.
         * @param ctx The shared FrameContext for this frame.
         */
        void update(FrameContext& ctx) override;

        /**
         * @brief Reads Animation; writes Transform.
         *
         * No structural Scene changes (only mutates fields of existing
         * Transforms that the animated entity already owns).
         */
        SystemAccess declareAccess() const override;

    private:

        /**
         * @brief Apply animation values to a Transform component.
         * @param animation The animation component.
         * @param transform The transform component to update.
         */
         void applyAnimation(const Animation& animation, Transform& transform) const;
};

} // namespace Engine
