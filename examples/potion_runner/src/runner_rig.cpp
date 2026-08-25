#include "runner_rig.h"

#include <cmath>
#include <cstdint>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

namespace {

// One full stride: both limbs swing out and back once. PotionRunner scales the
// Animator's speed with the run, so the cycle quickens as the track does - and
// the markers below quicken with it, because they are points on this timeline
// rather than a period of their own.
constexpr float STRIDE_PERIOD = 0.55f;

// Rotation keys per limb per cycle. The swing is a cosine and the sampler
// slerps between keys, so this is purely how finely the curve is resolved;
// thirty-two puts eleven degrees of phase between neighbours, under which the
// chord and the arc are indistinguishable at this scale.
constexpr uint32_t STRIDE_KEYS = 32;

/**
 * @brief One limb of the rig: where it hangs, how far it swings, and when.
 */
struct LimbSpec {
    const char* bone;
    glm::vec3   joint;      ///< Shoulder or hip, in the rig's frame.
    float       amplitude;  ///< Peak swing about X, in radians.
    float       phase;      ///< Seconds into the cycle this limb starts at.
};

// Opposing limbs - and the opposite arm and leg of each side - start half a
// cycle apart, which is what a stride is.
const LimbSpec LIMBS[] = {
    {RUNNER_BONE_ARM_L, {-0.46f,  0.30f, 0.0f}, 0.9f, 0.0f},
    {RUNNER_BONE_ARM_R, { 0.46f,  0.30f, 0.0f}, 0.9f, STRIDE_PERIOD * 0.5f},
    {RUNNER_BONE_LEG_L, {-0.18f, -0.28f, 0.0f}, 1.1f, STRIDE_PERIOD * 0.5f},
    {RUNNER_BONE_LEG_R, { 0.18f, -0.28f, 0.0f}, 1.1f, 0.0f},
};

// The swing the keyframe track used to describe with three keys and an
// easeInOutSine between them - which works out to exactly this cosine, so the
// baked clip reproduces the old motion rather than approximating it.
float swingAngle(float amplitude, float seconds) {
    return -amplitude * std::cos(glm::two_pi<float>() * seconds / STRIDE_PERIOD);
}

} // namespace

SkeletonAsset makeRunnerSkeleton() {
    SkeletonAsset rig;

    const auto addBone = [&rig](const char* name, int32_t parent, const glm::vec3& position) {
        rig.bones.push_back({name, parent});
        Transform bind;
        bind.position = position;
        rig.bindPose.push_back(bind);
        // Parented straight to the root, whose bind is identity, so a bone's
        // model-space bind matrix is its own. Nothing here is skinned, but the
        // inverse bind is what the asset means and a placeholder would be a lie
        // the first time something is.
        rig.inverseBind.push_back(glm::inverse(Transform::computeModelMatrix(bind)));
    };

    addBone("Root", -1, glm::vec3(0.0f));
    for (const LimbSpec& limb : LIMBS) addBone(limb.bone, 0, limb.joint);
    return rig;
}

AnimationClipAsset makeRunnerStride() {
    AnimationClipAsset clip;
    clip.skeleton = RUNNER_RIG_NAME;
    clip.duration = STRIDE_PERIOD;
    // Parallel to the rig: the root plus the four limbs, and the root carries
    // no channel at all, so the bind pose stands for it.
    clip.bones.resize(1 + std::size(LIMBS));

    clip.rotationTimes.reserve(std::size(LIMBS) * (STRIDE_KEYS + 1));
    clip.rotations.reserve(clip.rotationTimes.capacity());

    for (size_t i = 0; i < std::size(LIMBS); ++i) {
        const LimbSpec& limb = LIMBS[i];
        ClipBone& bone = clip.bones[i + 1];
        bone.rotation  = {static_cast<uint32_t>(clip.rotations.size()), STRIDE_KEYS + 1};

        // The closing key repeats the opening one, so a head that wraps reads
        // the same pose either side of the seam instead of stepping.
        for (uint32_t k = 0; k <= STRIDE_KEYS; ++k) {
            const float time = STRIDE_PERIOD * static_cast<float>(k) / static_cast<float>(STRIDE_KEYS);
            clip.rotationTimes.push_back(time);
            clip.rotations.push_back(
                glm::angleAxis(swingAngle(limb.amplitude, time + limb.phase), Math::WORLD_AXIS_X));
        }
    }

    // A quarter and three quarters through, both legs are vertical: one is
    // planted and the other is swinging past it, and the planted foot is at the
    // bottom of its arc. Two footfalls a cycle, which is what a stride is.
    clip.markers.push_back({RUNNER_MARKER_FOOTSTEP, STRIDE_PERIOD * 0.25f});
    clip.markers.push_back({RUNNER_MARKER_FOOTSTEP, STRIDE_PERIOD * 0.75f});
    return clip;
}

} // namespace Vkm::Engine
