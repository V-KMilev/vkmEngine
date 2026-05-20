#pragma once

#include "core/system.h"

namespace Engine {

struct Animation;
struct Transform;

/**
 * @brief Advances every Animation component and writes its result into the
 *        entity's Transform.
 *
 * Registered at SystemStage::Simulation. Each update:
 *  - Advances animation.time by deltaTime * speed.
 *  - Applies positionTrack / rotationTrack / scaleTrack values to the
 *    Transform via easing-aware sampling.
 *  - Handles playback control (playing, looping, speed).
 *  - Marks the touched subtrees dirty so HierarchySystem rebuilds their
 *    WorldTransforms downstream the same frame.
 *
 * The inner per-track loop is parallelised over playing entities; the
 * dirty-mark pass stays serial (markDirty cascades into the same
 * Hierarchy memory).
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
