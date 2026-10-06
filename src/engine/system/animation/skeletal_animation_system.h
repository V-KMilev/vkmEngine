#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "debug/fault_latch.h"
#include "ecs/component/animation/animator.h"
#include "ecs/entity.h"
#include "system/animation/pose_buffer.h"
#include "system/animation/ragdoll_pose.h"
#include "system/animation/pose_evaluator.h"

namespace Vkm::Engine {

struct Ragdoll;

class ResourceManager;
class Scene;
struct AnimationClipAsset;
struct SkeletonAsset;

/**
 * @brief Poses every rig in the scene and publishes the result on FrameContext::poses.
 *
 * Runs in SystemStage::Simulation, since it advances clip time, ahead of whatever reads
 * FrameContext::poses, and writes no Transform. Only the evaluation is parallel; marker announcement is
 * serial, because the EventBus is main-thread only.
 *
 * Time advances on the tick. The pose is also composed every paused frame, so a scrubbed Animator shows
 * its pose, and on the first frame after the world is replaced, because the buffer maps entity slots
 * the new world reuses. Composition is idempotent.
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
        void fixedUpdate(FrameContext& ctx) override;

    private:
        /**
         * @brief One rig to pose this frame, with its assets already resolved.
         *
         * Resolved serially, so the parallel phase never touches the ResourceManager and a rig whose
         * skeleton went away is dropped before it is indexed.
         */
        struct RigWork {
            uint32_t animatorIndex = 0;  ///< Dense index into the Animator storage.
            uint32_t entityIndex   = 0;  ///< Entity slot carrying the Animator.
            uint32_t slice         = 0;  ///< Slice addSlice() handed out for it.

            const SkeletonAsset* skeleton = nullptr;

            /**
             * @brief The ragdoll driving this rig, or null when a clip is.
             */
            const Ragdoll* ragdoll   = nullptr;
            uint32_t       firstBody = 0;                ///< Where its bodies' poses start in m_ragdollBodies
            glm::mat4      rigWorld  = glm::mat4(1.0f);  ///< The rig entity's world matrix

            const AnimationClipAsset* clip     = nullptr;  ///< Null holds the bind pose.
            const AnimationClipAsset* fadeClip = nullptr;  ///< Clip being faded out of; null when nothing is.

            /**
             * @brief What the playing head did this frame, for the serial pass to announce markers from.
             */
            PlaybackStep step;
        };

        /**
         * @brief Rebuild the whole pose buffer for one advance of the head.
         *
         * A repose outside a tick advances by nothing and announces nothing.
         *
         * @param ctx Frame context; receives the published pose buffer.
         * @param step Seconds playback advances by.
         * @param announceMarkers Whether crossed clip markers reach the bus.
         */
        void run(FrameContext& ctx, float step, bool announceMarkers);

        /**
         * @brief Pose every rig in the scene into the already-cleared buffer.
         *
         * @param ctx Scene to walk and assets to resolve against.
         * @param step Seconds playback advances by: the fixed step on a tick, zero outside one.
         */
        void poseRigs(FrameContext& ctx, float step);

        /**
         * @brief Whether @p skeleton holds what the composers index unchecked, naming each gap once.
         *
         * A cooked skeleton was judged on the way in; this catches one built in code.
         *
         * @param skeleton Rig about to be allocated a slice.
         * @return True when the rig can be posed.
         */
        bool isPoseable(const SkeletonAsset& skeleton);

        /**
         * @brief Resolve @p handle to a clip this rig can actually play.
         *
         * Null for an empty or dead handle, one findClipFault refuses, or one that does not fit this rig
         * by name or bone count - named once per gap rather than posing the wrong joints.
         *
         * @param resources Assets the handle resolves against.
         * @param handle Clip handle off the Animator.
         * @param skeleton Rig it has to belong to.
         * @return The clip, or nullptr to hold the bind pose.
         */
        const AnimationClipAsset* resolveClip(
            const ResourceManager& resources,
            const AnimationClipHandle& handle,
            const SkeletonAsset& skeleton
        );

        /**
         * @brief Record @p work's slice as the pose of every descendant of @p entity, stopping where a
         *        nested Animator takes over.
         *
         * Recursive, so bounded by HierarchyOperations::MAX_DEPTH.
         *
         * @param scene Scene holding the hierarchy.
         * @param resources Assets the mesh handles resolve against.
         * @param entity Entity whose children are stamped.
         * @param work The rig doing the posing.
         * @param depth Steps already taken below the rig; 0 at the rig itself.
         */
        void stampDescendants(
            Scene& scene,
            const ResourceManager& resources,
            EntityId entity,
            const RigWork& work,
            uint32_t depth
        );

        /**
         * @brief Enqueue an AnimationEvent for every marker a rig crossed this tick.
         *
         * @param ctx Supplies the scene and the event bus.
         */
        void publishMarkers(FrameContext& ctx);

        /**
         * @brief Name the two ways a skinned mesh can be wrong about its rig.
         *
         * Both are silent and unrecoverable, so reported, not repaired: a mesh skinned to another rig
         * poses the wrong joints, and a mesh off its rig's origin is transformed twice (palette, then its
         * own transform).
         *
         * @param scene Scene holding the components.
         * @param resources Assets the mesh handle resolves against.
         * @param entity Entity being stamped.
         * @param skeleton Rig posing it.
         * @param onRig True when @p entity is the Animator itself, which the offset test skips.
         */
        void checkSkinnedMesh(
            const Scene& scene,
            const ResourceManager& resources,
            EntityId entity,
            const SkeletonAsset& skeleton,
            bool onRig
        );

    private:
        PoseBuffer m_poses;
        std::vector<RigWork> m_work;  ///< Rebuilt each frame; keeps its capacity.

        /// Every ragdoll-driven rig's bodies, back to back; keeps its capacity.
        std::vector<RagdollBodyPose> m_ragdollBodies;

        uint64_t m_epoch = 0;  ///< Scene::epoch() the pose buffer was last composed in.

        FaultLatch m_badSkeleton;   ///< A skeleton findSkeletonFault refuses.
        FaultLatch m_badClip;       ///< A clip findClipFault refuses.
        FaultLatch m_clipMismatch;  ///< A clip cooked against another rig.
        FaultLatch m_rigMismatch;   ///< A mesh skinned to another rig.
        FaultLatch m_meshOffset;    ///< A skinned mesh sitting off its rig's origin.
};

} // namespace Vkm::Engine
