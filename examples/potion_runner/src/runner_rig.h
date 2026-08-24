#pragma once

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief The runner's stride: a five-bone rig, and the one clip that swings it.
 *
 * Built in code like every other asset this project uses - the game generates
 * its world rather than loading one - and built as a rig rather than as four
 * keyframed pivots for one reason: a clip can carry markers, and a pivot cannot.
 * The stride announces a footstep at each instant a leg passes under the body,
 * which is when that foot is on the ground, so the sound is a property of the
 * animation instead of a timer running beside it. Speed the cadence up and the
 * footsteps speed up with it because they are the same clock.
 *
 * Nothing here is skinned. The visible limbs are boxes hung on the bones with
 * BoneSockets, the same relationship a weapon has to a hand.
 */

/// Asset names; the Animator resolves the rig and the clip by them.
inline constexpr const char* RUNNER_RIG_NAME  = "potion:rig";
inline constexpr const char* RUNNER_CLIP_NAME = "potion:stride";

// The four bones a limb hangs off. The rig also carries a root, which nothing
// hangs off: it is the frame the four are placed in.
inline constexpr const char* RUNNER_BONE_ARM_L = "Arm L";
inline constexpr const char* RUNNER_BONE_ARM_R = "Arm R";
inline constexpr const char* RUNNER_BONE_LEG_L = "Leg L";
inline constexpr const char* RUNNER_BONE_LEG_R = "Leg R";

/// What the stride announces twice a cycle: a foot has reached the ground.
inline constexpr const char* RUNNER_MARKER_FOOTSTEP = "footstep";

/**
 * @brief Build the runner's rig: a root, and the four limb bones under it.
 *
 * Each limb bone sits at its shoulder or hip in the bind pose, so a socket on
 * it lands on the joint and the clip's rotation swings the limb about that
 * joint rather than paddling it about its own centre.
 *
 * @return The rig, named RUNNER_RIG_NAME so the clip below can bind to it.
 */
SkeletonAsset makeRunnerSkeleton();

/**
 * @brief Build the looping run cycle for that rig, footstep markers included.
 *
 * One rotation channel per limb, baked from the cosine the old eased keyframe
 * track described exactly, with opposing limbs half a cycle apart. The two
 * markers sit at the quarter and three-quarter points, which is where both legs
 * are vertical: the swinging foot is at the bottom of its arc, which is a
 * footfall.
 *
 * @return The clip, named RUNNER_CLIP_NAME and bound to RUNNER_RIG_NAME.
 */
AnimationClipAsset makeRunnerStride();

} // namespace Vkm::Engine
