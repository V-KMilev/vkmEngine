#pragma once

#include "core/system.h"

namespace Vkm::Engine {

struct Animation;
struct Transform;

/**
 * @brief Advances every Animation component and writes its result into the entity's local Transform.
 *
 * Runs on the fixed step, so a paused frame does not re-sample and clobber an authored Transform.
 * Runs in SystemStage::Simulation, ahead of HierarchySystem, which resolves what it writes that frame.
 */
class AnimationSystem : public System {
    public:
        AnimationSystem() = default;
        ~AnimationSystem() override = default;

        AnimationSystem(const AnimationSystem& other) = delete;
        AnimationSystem& operator=(const AnimationSystem& other) = delete;

        AnimationSystem(AnimationSystem && other) = delete;
        AnimationSystem& operator=(AnimationSystem && other) = delete;

    public:
        void fixedUpdate(FrameContext& ctx) override;

        /**
         * @brief Pose @p transform as @p animation stands at its playhead.
         *
         * An empty track leaves its channel as authored. Callable while the world is not stepping, to
         * show a scrubbed playhead.
         *
         * @param animation The clip and its playhead.
         * @param transform Pose written.
         */
        static void applyAnimation(const Animation& animation, Transform& transform);
};

} // namespace Vkm::Engine
