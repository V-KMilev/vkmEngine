#pragma once

#include "core/system.h"

namespace Vkm::Engine {

struct Animation;
struct Transform;

/**
 * @brief Advances every Animation component and writes its result into the
 *        entity's Transform.
 *
 * Registered at SystemStage::Simulation. Skipped entirely when no simulation
 * time elapsed this frame (paused), so an authored Transform is not clobbered
 * by re-sampling the track at an unchanged time. HierarchySystem runs later the
 * same frame and rebuilds every WorldTransform, so an animated entity inside a
 * hierarchy needs nothing recorded here.
 *
 * The one pass (advance time + write Transform) runs in parallel over all
 * animation slots, skipping non-playing ones.
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
        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief Apply animation values to a Transform component.
         * @param animation The animation component.
         * @param transform The transform component to update.
         */
        static void applyAnimation(const Animation& animation, Transform& transform);
};

} // namespace Vkm::Engine
