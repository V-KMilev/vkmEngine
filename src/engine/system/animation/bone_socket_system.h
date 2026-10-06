#pragma once

#include "core/system.h"
#include "debug/fault_latch.h"

namespace Vkm::Engine {

/**
 * @brief Places every entity carrying a BoneSocket on its bone, from the pose SkeletalAnimationSystem
 *        published this frame.
 *
 * Runs in SystemStage::Transform: after the Simulation stage that produces `ctx.poses` (reading it there
 * would race registration order), and ahead of HierarchySystem, which turns the *local* Transform written
 * here into a world matrix the same frame. Not gated on simulation time: scrubbing an Animator has to
 * move what the character holds.
 */
class BoneSocketSystem : public System {
    public:
        BoneSocketSystem() = default;
        ~BoneSocketSystem() override = default;

        BoneSocketSystem(const BoneSocketSystem& other) = delete;
        BoneSocketSystem& operator=(const BoneSocketSystem& other) = delete;

        BoneSocketSystem(BoneSocketSystem && other) = delete;
        BoneSocketSystem& operator=(BoneSocketSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief Write every socket's local Transform from its bone.
         *
         * @param ctx Scene, assets and this frame's pose.
         */
        void placeSockets(FrameContext& ctx);

    private:
        // Each is silent on screen (an unplaced socket stays where it was), so each is logged; one latch
        // per fault, so one never silences another.
        FaultLatch m_noPublishedPose;  ///< No pose was published this frame.
        FaultLatch m_noTransform;      ///< The socket entity has no Transform to place.
        FaultLatch m_unrooted;         ///< Not a direct child of an entity carrying an Animator.
        FaultLatch m_noSkeleton;       ///< The rig's Animator names no live skeleton.
        FaultLatch m_bonelessSkeleton; ///< The rig's skeleton has no bones.
        FaultLatch m_noBone;           ///< The rig has no bone of that name.
};

} // namespace Vkm::Engine
