#pragma once

#include "ecs/component/animator.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

struct AnimationClipAsset;
struct SkeletonAsset;

/**
 * @brief The clips a rig reads this frame, and how much of each.
 *
 * One, or two while a crossfade is in flight: the clip being left is sampled
 * alongside the one being entered, at its own playback head, because a blend
 * between two moving poses is what keeps a run-to-walk from freezing a foot.
 *
 * Two is the whole of 1.6's blending. What grows later - a list of layers, a
 * blend tree - replaces this struct, which is per-frame, rather than the
 * persisted Animator; that is why the fade fields on it are transient.
 */
struct PoseSample {
    const AnimationClipAsset* clip = nullptr;  ///< Clip playing; null holds the bind pose.
    float time = 0.0f;                         ///< Its playback head, seconds.

    const AnimationClipAsset* from = nullptr;  ///< Clip being left; null when nothing is fading.
    float fromTime = 0.0f;                     ///< Its own playback head.

    float weight = 1.0f;  ///< How much of `clip` is in the result: 0 is all `from`, 1 is all `clip`.
};

/**
 * @brief Move @p animator's playback head(s) on by one frame, honouring loop and
 *        end of clip, and run down any fade in flight.
 *
 * Nothing moves while simulation time is stopped, so authoring a time while
 * paused is not immediately overwritten - the same rule AnimationSystem holds
 * for its tracks. A stopped animator holds its own head, but a fade in flight
 * still runs down: a one-shot clip that ends mid-blend would otherwise leave
 * the character at a weight no field names and nothing clears.
 *
 * Wrapping is a floor-subtract rather than a modulo because a negative speed
 * has to come round to the end of the clip, and fmod of a negative time stays
 * negative. A clip run to its end without looping stops rather than clamping
 * silently, so `playing` reports what actually happened.
 *
 * The outgoing clip of a fade advances by the same delta but never stops the
 * animator: what is playing is the clip that was faded *to*, and an outgoing
 * one that runs out simply holds its last frame for the rest of the blend. The
 * fade itself counts down in unscaled simulation seconds, so a blend length is a
 * duration the caller can predict rather than one `speed` moves, and it reaches
 * its end whether or not the clip it is entering is still running.
 *
 * @param animator Animator to advance, in place.
 * @param duration Length of the clip playing on it, in seconds; 0 disables wrapping.
 * @param fromDuration Length of the clip being faded out of; 0 disables its wrapping.
 * @param simDelta Simulation seconds elapsed this frame.
 */
void advancePlayback(Animator& animator, float duration, float fromDuration, float simDelta);

/**
 * @brief Sample @p sample's clips and compose the rig's pose, palette and
 *        bounds into @p out, in one forward sweep over the bones.
 *
 * Free rather than a method on SkeletalAnimationSystem because it is a pure
 * function of the animation data: the system decides which rigs to pose and
 * when, this decides what a pose is. That also makes it directly checkable
 * against a hand-built skeleton at known times, which is the only way the
 * composed matrices get verified - a wrong multiply order looks entirely
 * plausible on screen.
 *
 * There is no intermediate array of local transforms. `parent < index` is a
 * validated format invariant, so a bone's parent is already composed by the
 * time the bone is reached and its local TRS never has to outlive one
 * iteration. A crossfade blends on that local TRS inside the same iteration,
 * before composition: blending composed matrices pulls a limb toward the
 * midpoint of two world positions and shortens it.
 *
 * A clip whose per-bone table is not parallel to @p skeleton is a clip bound to
 * a different rig; it is ignored and the bind pose stands, rather than indexed
 * past its end. That is checked for both clips independently, so a bad outgoing
 * clip cannot take the incoming one down with it.
 *
 * @param skeleton Rig being posed. Its three vectors are parallel and its bones
 *                 are ordered parent-before-child - both validated where a
 *                 skeleton is read or built, neither re-checked per frame here.
 * @param sample What to sample: one clip, or two and a weight.
 * @param out Slice to write, sized for the skeleton's bone count.
 */
void composePose(
    const SkeletonAsset& skeleton,
    const PoseSample& sample,
    const PoseWrite& out
);

} // namespace Vkm::Engine
