#pragma once

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Places every entity carrying a BoneSocket on the bone it names, out of
 *        the pose SkeletalAnimationSystem published this frame.
 *
 * Registered at SystemStage::Transform, ahead of HierarchySystem, and both
 * neighbours are load-bearing. After the pose, because `ctx.poses` is a
 * per-frame product of the Simulation stage and reading it from Simulation would
 * race the producer's registration order. Before the world resolve, because this
 * writes the socket's *local* Transform and lets HierarchySystem turn it into a
 * world matrix the same frame - writing its WorldTransform instead would trail
 * the character by a frame, and leave a muzzle flash parented under the socket
 * resolving against the frame before that.
 *
 * Placement is not gated on simulation time, for the reason composition is not:
 * scrubbing an Animator while paused has to move what the character is holding.
 *
 * A scene with no sockets pays one null storage check a frame.
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
         * @brief What this frame's pass ran into, so a latch clears once the
         *        fault it named is gone instead of staying stuck after a fix.
         */
        struct FaultsSeen {
            bool noPose    = false;
            bool unrooted  = false;
            bool noBone    = false;
        };

        /**
         * @brief Write every socket's local Transform from its bone.
         *
         * The whole pass lives here so update() has a single place to write the
         * latches from: an early exit - no sockets at all - clears them like any
         * other frame, which is what stops a fault that has gone away from
         * suppressing its own next report.
         *
         * @param ctx Frame context: the scene to walk, the assets the rigs
         *        resolve against, and this frame's pose.
         * @param seen Collects the faults this frame ran into.
         */
        void placeSockets(FrameContext& ctx, FaultsSeen& seen);

    private:
        // Edge latches, so each fault is named once per gap and not once a
        // frame. All three are silent on screen: an unplaced socket stays where
        // it last was, which for a moved one is a plausible-looking lie.
        bool m_noPoseLogged   = false;  ///< Nothing posed the rig this socket hangs off.
        bool m_unrootedLogged = false;  ///< Not a direct child of an entity carrying an Animator.
        bool m_noBoneLogged   = false;  ///< The rig has no bone of that name.
};

} // namespace Vkm::Engine
