#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;
struct AnimationClipAsset;
struct SkeletonAsset;

/**
 * @brief Poses every rig in the scene and publishes the result on
 *        FrameContext::poses.
 *
 * Registered at SystemStage::Simulation immediately after AnimationSystem,
 * because it advances clip time and that is state over time; it has to precede
 * the Visibility stage, which bounds a posed character, and the Render stage,
 * which draws one. It writes no Transform, so it cannot contend with
 * AnimationSystem, PhysicsSystem or HierarchySystem - which is a direct
 * consequence of bones being indices rather than entities.
 *
 * Four phases. Allocation and mapping are serial and cheap (one pass over the
 * Animators, one walk of their subtrees); only the evaluation is parallel, and
 * it is safe for the same reason AnimationSystem's is - each rig writes its own
 * disjoint slice of the pose arrays and its own Animator.
 *
 * Time advances only when simulation time elapsed, but composition runs every
 * frame regardless, so scrubbing an Animator in the editor while paused shows
 * the pose it names. Composition is idempotent, so that costs nothing.
 */
class SkeletalAnimationSystem : public System {
    public:
        SkeletalAnimationSystem() = default;
        ~SkeletalAnimationSystem() override = default;

        SkeletalAnimationSystem(const SkeletalAnimationSystem& other) = delete;
        SkeletalAnimationSystem& operator=(const SkeletalAnimationSystem& other) = delete;

        SkeletalAnimationSystem(SkeletalAnimationSystem && other) = delete;
        SkeletalAnimationSystem& operator=(SkeletalAnimationSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief One rig to pose this frame, with its assets already resolved.
         *
         * The handles are resolved once, serially, so the parallel phase never
         * touches the ResourceManager - and so a rig whose skeleton went away
         * is dropped before it can be indexed.
         */
        struct RigWork {
            uint32_t animatorIndex = 0;  ///< Dense index into the Animator storage.
            uint32_t entityIndex   = 0;  ///< Entity slot carrying the Animator.
            uint32_t slice         = 0;  ///< Slice addSlice() handed out for it.

            const SkeletonAsset*      skeleton = nullptr;
            const AnimationClipAsset* clip     = nullptr;  ///< Null holds the bind pose.
        };

        /**
         * @brief What this frame's walk ran into, so a latch clears once the
         *        fault it named is gone instead of staying stuck after a fix.
         */
        struct FaultsSeen {
            bool clipMismatch = false;
            bool rigMismatch  = false;
            bool meshOffset   = false;
        };

        /**
         * @brief Pose every rig in the scene into the already-cleared buffer.
         *
         * The whole walk lives here so update() has a single place to write the
         * latches from: an early exit - no Animators, or none whose rig is still
         * alive - clears them like any other frame, which is what stops a fault
         * that has gone away from suppressing its own next report.
         *
         * @param ctx Frame context: the scene to walk, the assets to resolve
         *        against, and the clock that advances playback.
         * @param seen Collects the faults this frame ran into.
         */
        void poseRigs(FrameContext& ctx, FaultsSeen& seen);

        /**
         * @brief Record @p work's slice as the pose of every descendant of
         *        @p entity, stopping wherever a nested Animator takes over.
         *
         * @param scene Scene holding the hierarchy.
         * @param resources Assets the mesh handles resolve against.
         * @param entity Entity whose children are stamped.
         * @param work The rig doing the posing.
         * @param seen Collects the faults the walk finds.
         */
        void stampDescendants(Scene& scene, const ResourceManager& resources,
                              EntityId entity, const RigWork& work, FaultsSeen& seen);

        /**
         * @brief Name the two ways a skinned mesh can be wrong about its rig.
         *
         * Both are silent by nature and neither is recoverable at runtime, so
         * they are reported rather than repaired: a mesh skinned to another rig
         * poses the wrong joints out of matching indices, and a mesh sitting off
         * its rig's origin is transformed twice - once by the palette, which
         * already resolves into rig space, and once by its own transform. Either
         * looks plausible for exactly one pose.
         *
         * @param scene Scene holding the components.
         * @param resources Assets the mesh handle resolves against.
         * @param entity Entity being stamped.
         * @param skeleton Rig posing it.
         * @param seen Collects what was found.
         */
        void checkSkinnedMesh(const Scene& scene, const ResourceManager& resources,
                              EntityId entity, const SkeletonAsset& skeleton,
                              FaultsSeen& seen);

    private:
        PoseBuffer m_poses;
        std::vector<RigWork> m_work;  ///< Rebuilt each frame; keeps its capacity.

        // Edge latches, so each fault is named once per gap rather than once a frame.
        bool m_clipMismatchLogged = false;  ///< A clip cooked against another rig.
        bool m_rigMismatchLogged  = false;  ///< A mesh skinned to another rig.
        bool m_meshOffsetLogged   = false;  ///< A skinned mesh sitting off its rig's origin.
};

} // namespace Vkm::Engine
