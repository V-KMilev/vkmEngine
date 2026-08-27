#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
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
 * Allocation and mapping are serial and cheap (one pass over the Animators, one
 * walk of their subtrees); only the evaluation is parallel, and it is safe for
 * the same reason AnimationSystem's is - each rig writes its own disjoint slice
 * of the pose arrays and its own Animator. Announcing the markers each rig
 * crossed comes last and is serial again, because the EventBus is main-thread
 * only and the evaluate pass has no business publishing anything.
 *
 * Time advances on the tick, because a clip is simulation. The pose those
 * times name is composed there too, and again on every frame the clock is
 * paused - which is what lets an Animator scrubbed in the editor show the pose
 * its time names, with no tick to run. Composition is idempotent, so the paused
 * path costs one rebuild and changes nothing else.
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
        bool hasFixedUpdate() const override { return true; }

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

            /**
             * @brief The ragdoll driving this rig, or null when a clip is.
             *
             * Resolved in the serial pass with everything else the parallel one
             * reads, so the evaluation never touches the scene.
             */
            const Ragdoll* ragdoll = nullptr;
            std::vector<RagdollBodyPose> ragdollBodies;  ///< Its bodies' poses, as values
            glm::mat4 rigWorld = glm::mat4(1.0f);  ///< The rig entity's world matrix

            const AnimationClipAsset* clip     = nullptr;  ///< Null holds the bind pose.
            const AnimationClipAsset* fadeClip = nullptr;  ///< Clip being faded out of; null when nothing is.

            /**
             * @brief What the playing head did this frame, recorded by the
             *        parallel pass so the serial one can announce markers.
             *
             * Three floats on a struct that is already per-rig and disjoint
             * across threads, which is what makes deferring the announcement
             * free - and it has to be deferred, because the EventBus is main
             * thread only.
             */
            PlaybackStep step;
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
         * @brief Resolve @p handle to a clip this rig can actually play.
         *
         * Answers null for an empty or dead handle, and for one that does not
         * fit this rig by name or by bone count - which is named once per gap
         * rather than silently posing the wrong joints out of matching indices.
         *
         * @param resources Assets the handle resolves against.
         * @param handle Clip handle off the Animator.
         * @param skeleton Rig it has to belong to.
         * @param seen Collects the mismatch if there is one.
         * @return The clip, or nullptr to hold the bind pose.
         */
        const AnimationClipAsset* resolveClip(const ResourceManager& resources,
                                              const AnimationClipHandle& handle,
                                              const SkeletonAsset& skeleton,
                                              FaultsSeen& seen);

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
         * @brief Enqueue an AnimationEvent for every marker a rig crossed this
         *        frame.
         *
         * Serial, after the evaluate pass, because the EventBus is main-thread
         * only - and cheap, because a scene holds few rigs and an unmarked clip
         * costs one branch.
         *
         * Only the clip an Animator is *playing* announces anything. A
         * crossfade advances two heads, and letting the outgoing one speak
         * would fire a footstep from the run a character has already left at
         * the same time as one from the walk it is entering.
         *
         * @param ctx Frame context supplying the scene and the event bus.
         */
        void publishMarkers(FrameContext& ctx);

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
         * @param onRig True when @p entity is the Animator itself, whose own
         *        transform is the rig's and places the character rather than
         *        doubling it - the offset test asks about descendants only.
         * @param seen Collects what was found.
         */
        void checkSkinnedMesh(const Scene& scene, const ResourceManager& resources,
                              EntityId entity, const SkeletonAsset& skeleton,
                              bool onRig, FaultsSeen& seen);

    private:
        PoseBuffer m_poses;
        std::vector<RigWork> m_work;  ///< Rebuilt each frame; keeps its capacity.

        // Edge latches, so each fault is named once per gap rather than once a frame.
        bool m_clipMismatchLogged = false;  ///< A clip cooked against another rig.
        bool m_rigMismatchLogged  = false;  ///< A mesh skinned to another rig.
        bool m_meshOffsetLogged   = false;  ///< A skinned mesh sitting off its rig's origin.
};

} // namespace Vkm::Engine
