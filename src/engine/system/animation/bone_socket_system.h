#pragma once

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Places every entity carrying a BoneSocket on the bone it names, out of
 *        the pose SkeletalAnimationSystem published this frame.
 *
 * Registered at SystemStage::Transform, ahead of HierarchySystem, because a
 * socket is a derived transform and that is the stage derived transforms belong
 * to. Both neighbours in that ordering are load-bearing:
 *
 * - **After the pose.** `ctx.poses` is a per-frame product of the Simulation
 *   stage. Reading it from Simulation would race the producer's registration
 *   order; reading it from Transform cannot.
 * - **Before the world resolve.** This writes the socket entity's *local*
 *   Transform and lets HierarchySystem turn it into a world matrix in the same
 *   frame, so the socket lands on the bone on the frame the character moves -
 *   including the very first frame, when there is no previous frame to have
 *   cached anything. The alternative, writing the socket's WorldTransform from
 *   the rig's, would read a matrix HierarchySystem last wrote a frame ago and
 *   trail the character by exactly one frame while it runs, which is invisible
 *   while it stands still. It would also leave anything parented *under* the
 *   socket - a muzzle flash under a gun - resolving against last frame's socket,
 *   because that walk has already happened by then.
 *
 * Placement is unconditional, not gated on simulation time, for the reason
 * composition is: scrubbing an Animator while paused has to move what the
 * character is holding, or a paused preview shows a pose the props disagree
 * with.
 *
 * A scene with no sockets pays one null storage check a frame and touches
 * nothing else - not the pose, not the hierarchy.
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
        // Edge latches, so each fault is named once per gap rather than once a
        // frame. All three are silent on screen otherwise: a socket that is not
        // placed simply stays wherever it last was, which for a fresh one is the
        // world origin and for a moved one is a plausible-looking lie.
        bool m_noPoseLogged   = false;  ///< Nothing posed the rig this socket hangs off.
        bool m_unrootedLogged = false;  ///< Not a direct child of an entity carrying an Animator.
        bool m_noBoneLogged   = false;  ///< The rig has no bone of that name.
};

} // namespace Vkm::Engine
