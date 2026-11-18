#pragma once

#include "ecs/component/animation/animator.h"
#include "core/frame/pose_buffer.h"

namespace Vkm::Engine {

struct AnimationClipAsset;
struct SkeletonAsset;

/**
 * @brief The clips a rig reads this frame, how much of each, and what a behavior adds on top.
 *
 * Two clips while a crossfade is in flight, each at its own playback head, so a blend between two moving
 * poses does not freeze a foot.
 */
struct PoseSample {
    const AnimationClipAsset* clip = nullptr;  ///< Clip playing; null holds the bind pose.
    float                     time = 0.0f;     ///< Its playback head, seconds.

    const AnimationClipAsset* from     = nullptr;  ///< Clip being left; null when nothing is fading.
    float                     fromTime = 0.0f;     ///< Its own playback head.

    float weight = 1.0f;  ///< 0 is all `from`, 1 is all `clip`.

    const BoneAdjust* adjust      = nullptr;  ///< The Animator's adjustments, applied after the blend; none
                                              ///< when null. A view of the component's list: nothing may
                                              ///< write that Animator while the sample is read
                                              ///< (SkeletalAnimationSystem::poseRigs).
    uint32_t          adjustCount = 0;
};

/**
 * @brief What @p animator shows @p seconds past where its heads stand, the animator left as it is.
 *
 * The heads move and the fade runs down by advanceHeads, as a tick moves them, so a frame drawn between
 * ticks shows the pose of its own moment.
 *
 * @param animator The rig's animator, as its last tick left it.
 * @param clip     Its playing clip, resolved; null holds the bind pose.
 * @param from     The clip it fades out of, resolved; null when nothing fades.
 * @param seconds  Simulation seconds past the heads; zero samples them where they stand.
 * @return The sample, viewing @p animator's adjustments.
 */
PoseSample sampleAhead(
    const Animator& animator,
    const AnimationClipAsset* clip,
    const AnimationClipAsset* from,
    float seconds
);

/**
 * @brief Sample @p sample's clips and compose the rig's pose, palette and bounds into @p out, in one
 *        forward sweep over the bones.
 *
 * `parent < index` (findSkeletonFault) means a bone's parent is composed first, so no local array is
 * kept. A crossfade blends the local TRS before composition, because blending composed matrices shortens
 * limbs. A BoneAdjust lands on that local TRS after the blend; the parent's final matrix brings its
 * model-space parts into the bone's frame.
 *
 * @param skeleton Rig being posed; must pass findSkeletonFault, which is not re-checked here.
 * @param sample One clip, or two and a weight. Each must be this rig's and pass findClipFault, or be
 *               null; not re-checked here.
 * @param out Slice to write, sized for the skeleton's bone count.
 */
void composePose(const SkeletonAsset& skeleton, const PoseSample& sample, const PoseWrite& out);

} // namespace Vkm::Engine
