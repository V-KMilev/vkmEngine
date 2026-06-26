#pragma once

#include "core/system.h"

namespace Engine {

struct Animation;
struct Transform;

/**
 * @brief Advances every Animation component and writes its result into the
 *        entity's Transform.
 *
 * Registered at SystemStage::Simulation. Skipped entirely when no simulation
 * time elapsed this frame (paused), so an authored Transform is not clobbered
 * by re-sampling the track at an unchanged time. Each update:
 *  - Advances animation.time by simDeltaTime * speed, applying looping/stop.
 *  - Applies positionTrack / rotationTrack / scaleTrack values to the
 *    Transform via easing-aware sampling (only for non-empty tracks).
 *  - Marks the touched subtrees dirty so HierarchySystem rebuilds their
 *    WorldTransforms downstream the same frame.
 *
 * The evaluate pass (advance time + write Transform) runs in parallel over all
 * animation slots, skipping non-playing ones; the dirty-mark pass stays serial
 * because markDirty cascades over descendants in shared Hierarchy memory.
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

    private:

        /**
         * @brief Apply animation values to a Transform component.
         * @param animation The animation component.
         * @param transform The transform component to update.
         */
        void applyAnimation(const Animation& animation, Transform& transform) const;
};

} // namespace Engine
