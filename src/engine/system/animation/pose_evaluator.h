#pragma once

#include "ecs/component/animation/animator.h"
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
 * @brief The sweep a playback head made this frame: where it was, where it now
 *        is, and how far it actually went between the two.
 *
 * `to` is the head the Animator now carries - already wrapped, already clamped -
 * so a marker test built on it agrees exactly with the next frame's `from`,
 * which is the same float. That is what makes one crossing one event: the
 * arrival end of a sweep and the departure end of the next are the same value,
 * so neither a rounding step nor a wrap can put a marker in both or in neither.
 *
 * `travel` is signed and may exceed the clip's length, because a hitch or a
 * large time-scale can step a head over several whole loops. It is what
 * distinguishes standing still from having gone all the way round.
 */
struct PlaybackStep {
    float from   = 0.0f;  ///< Head before the advance.
    float to     = 0.0f;  ///< Head after it, as the Animator now carries it.
    float travel = 0.0f;  ///< Signed seconds covered; 0 when nothing moved.
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
 * @return The sweep the *playing* head made. A stopped animator, a paused
 *         frame and a zero speed all report no travel, which is what stops any
 *         of them from announcing a marker.
 */
PlaybackStep advancePlayback(Animator& animator, float duration, float fromDuration, float simDelta);

/**
 * @brief Whether @p step passed the instant @p marker names.
 *
 * The sweep is closed at the end it arrived at and open at the end it left, in
 * both directions: a head that lands exactly on a marker announces it, and
 * moving off again does not announce it a second time. Crossing it once more
 * means leaving and coming back.
 *
 * A step long enough to cover a whole loop announces every marker exactly once
 * rather than once per lap it skipped. The frame drew one pose, so it makes one
 * sound; replaying four laps' worth of footsteps into a single frame is the
 * burst a hitch would otherwise produce.
 *
 * Whether the clip loops is not asked, because @p step already says: a wrapped
 * sweep is one whose `to` lies behind its `from` in the direction of travel, and
 * a clamped one can never look like that.
 *
 * @param step The sweep this frame's advance made.
 * @param marker Time the marker names, in seconds into the clip.
 * @param duration Clip length in seconds; 0 makes every marker unreachable.
 * @return True when the marker should be announced this frame.
 */
bool crossesMarker(const PlaybackStep& step, float marker, float duration);

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
