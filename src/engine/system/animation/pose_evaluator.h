#pragma once

#include "ecs/component/animator.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

struct AnimationClipAsset;
struct SkeletonAsset;

/**
 * @brief Move @p animator's playback head on by one frame, honouring loop and
 *        end of clip.
 *
 * Nothing moves while simulation time is stopped or the animator is not
 * playing, so authoring a time while paused is not immediately overwritten -
 * the same rule AnimationSystem holds for its tracks.
 *
 * Wrapping is a floor-subtract rather than a modulo because a negative speed
 * has to come round to the end of the clip, and fmod of a negative time stays
 * negative. A clip run to its end without looping stops rather than clamping
 * silently, so `playing` reports what actually happened.
 *
 * @param animator Animator to advance, in place.
 * @param duration Length of the clip playing on it, in seconds; 0 disables wrapping.
 * @param simDelta Simulation seconds elapsed this frame.
 */
void advancePlayback(Animator& animator, float duration, float simDelta);

/**
 * @brief Sample @p clip at @p time and compose the rig's pose, palette and
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
 * iteration. Blending, when it comes, happens on that local TRS inside the same
 * iteration - before composition, because blending composed matrices skews
 * limbs.
 *
 * A clip whose per-bone table is not parallel to @p skeleton is a clip bound to
 * a different rig; it is ignored and the bind pose stands, rather than indexed
 * past its end.
 *
 * @param skeleton Rig being posed. Its three vectors are parallel and its bones
 *                 are ordered parent-before-child - both validated where a
 *                 skeleton is read or built, neither re-checked per frame here.
 * @param clip Clip to sample, or nullptr to hold the bind pose.
 * @param time Playback time in seconds, already wrapped into the clip's range.
 * @param out Slice to write, sized for the skeleton's bone count.
 */
void composePose(
    const SkeletonAsset& skeleton,
    const AnimationClipAsset* clip,
    float time,
    const PoseWrite& out
);

} // namespace Vkm::Engine
