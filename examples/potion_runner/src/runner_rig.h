#pragma once

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

namespace Potion {

using namespace Vkm::Engine;

// The runner's stride: a five-bone rig and the clip that swings it. A rig because a
// clip carries footstep markers, so the sound follows the animation, not a timer.
// Nothing is skinned: the limbs are boxes hung on the bones with BoneSockets.

/// Asset names the Animator resolves the rig and clip by.
inline constexpr const char* RUNNER_RIG_NAME  = "potion:rig";
inline constexpr const char* RUNNER_CLIP_NAME = "potion:stride";

// The four limb bones; the rig's root carries no limb.
inline constexpr const char* RUNNER_BONE_ARM_L = "Arm L";
inline constexpr const char* RUNNER_BONE_ARM_R = "Arm R";
inline constexpr const char* RUNNER_BONE_LEG_L = "Leg L";
inline constexpr const char* RUNNER_BONE_LEG_R = "Leg R";

/// Marker for a footfall, twice a cycle.
inline constexpr const char* RUNNER_MARKER_FOOTSTEP = "footstep";

/**
 * @brief Build the runner's rig: a root, and the four limb bones under it.
 *
 * Limb bones bind at the shoulder or hip, so the clip swings each limb about its joint.
 *
 * @return The rig; add it under RUNNER_RIG_NAME, which the clip binds to.
 */
SkeletonAsset makeRunnerSkeleton();

/**
 * @brief Build the looping run cycle for that rig, footstep markers included.
 *
 * Markers sit at the quarter and three-quarter points, where the legs are
 * vertical and the swinging foot is at the bottom of its arc.
 *
 * @return The clip, bound to RUNNER_RIG_NAME; add it under RUNNER_CLIP_NAME.
 */
AnimationClipAsset makeRunnerStride();

} // namespace Potion
