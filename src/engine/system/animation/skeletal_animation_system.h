#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

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
         * @brief Record @p slice as the pose of every descendant of @p entity,
         *        stopping wherever a nested Animator takes over.
         *
         * @param scene Scene holding the hierarchy.
         * @param entity Entity whose children are stamped.
         * @param slice Slice index to record.
         */
        void stampDescendants(Scene& scene, EntityId entity, uint32_t slice);

    private:
        PoseBuffer m_poses;
        std::vector<RigWork> m_work;  ///< Rebuilt each frame; keeps its capacity.

        bool m_clipMismatchLogged = false;  ///< Edge latch so a wrong-rig clip is named once per gap.
};

} // namespace Vkm::Engine
