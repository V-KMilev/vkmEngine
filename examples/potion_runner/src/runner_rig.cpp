#include "runner_rig.h"

#include <cmath>
#include <cstdint>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "ecs/component/core/transform.h"

namespace Potion {

namespace {

// One full stride; PotionRunner::updatePlayer scales the Animator's speed with the run.
constexpr float STRIDE_PERIOD = 0.55f;

// Rotation keys per limb per cycle: only how finely the cosine is resolved.
constexpr uint32_t STRIDE_KEYS = 32;

struct LimbSpec {
    const char* bone;
    glm::vec3   joint;      ///< Shoulder or hip, in the rig's frame.
    float       amplitude;  ///< Peak swing about X, in radians.
    float       phase;      ///< Seconds into the cycle this limb starts at.
};

// Opposing limbs, and each side's arm and leg, start half a cycle apart.
const LimbSpec LIMBS[] = {
    {RUNNER_BONE_ARM_L, {-0.46f,  0.30f, 0.0f}, 0.9f, 0.0f},
    {RUNNER_BONE_ARM_R, { 0.46f,  0.30f, 0.0f}, 0.9f, STRIDE_PERIOD * 0.5f},
    {RUNNER_BONE_LEG_L, {-0.18f, -0.28f, 0.0f}, 1.1f, STRIDE_PERIOD * 0.5f},
    {RUNNER_BONE_LEG_R, { 0.18f, -0.28f, 0.0f}, 1.1f, 0.0f},
};

// A cosine, so the cycle closes without a seam at STRIDE_PERIOD.
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
        // Parented to the root, whose bind is identity, so its model-space bind is its own.
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
    // Parallel to the rig; the root has no channel, so its bind pose stands.
    clip.bones.resize(1 + std::size(LIMBS));

    clip.rotationTimes.reserve(std::size(LIMBS) * (STRIDE_KEYS + 1));
    clip.rotations.reserve(clip.rotationTimes.capacity());

    for (size_t i = 0; i < std::size(LIMBS); ++i) {
        const LimbSpec& limb = LIMBS[i];
        ClipBone& bone = clip.bones[i + 1];
        bone.rotation  = {static_cast<uint32_t>(clip.rotations.size()), STRIDE_KEYS + 1};

        // The closing key repeats the opening one, so the loop seam does not step.
        for (uint32_t k = 0; k <= STRIDE_KEYS; ++k) {
            const float time = STRIDE_PERIOD * static_cast<float>(k) / static_cast<float>(STRIDE_KEYS);
            clip.rotationTimes.push_back(time);
            clip.rotations.push_back(
                glm::angleAxis(swingAngle(limb.amplitude, time + limb.phase), Math::WORLD_AXIS_X)
            );
        }
    }

    clip.markers.push_back({RUNNER_MARKER_FOOTSTEP, STRIDE_PERIOD * 0.25f});
    clip.markers.push_back({RUNNER_MARKER_FOOTSTEP, STRIDE_PERIOD * 0.75f});
    return clip;
}

} // namespace Potion
