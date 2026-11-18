#include "system/animation/pose/clip_pose.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "base/math/rotation.h"
#include "ecs/component/scene/transform.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "system/animation/playback.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief The two keys bracketing a time, and how far between them it falls.
 */
struct KeyPair {
    uint32_t a = 0;
    uint32_t b = 0;
    float    t = 0.0f;
};

// Both ends clamp rather than extrapolate, so a channel covering part of the timeline holds, not drifts.
KeyPair locateKeys(const std::vector<float>& times, const ClipChannel& channel, float time) {
    const auto begin = times.begin() + channel.first;
    const auto end   = begin + channel.count;

    const auto it = std::upper_bound(begin, end, time);
    if (it == begin) return {channel.first, channel.first, 0.0f};
    if (it == end) {
        const uint32_t last = channel.first + channel.count - 1;
        return {last, last, 0.0f};
    }

    const auto b     = static_cast<uint32_t>(it - times.begin());
    const uint32_t a = b - 1;
    const float span = times[b] - times[a];
    return {a, b, span > 0.0f ? (time - times[a]) / span : 0.0f};
}

// Only channels the clip holds are written; the rest keep their bind value.
void sampleBone(const AnimationClipAsset& clip, uint32_t bone, float time, Transform& local) {
    const ClipBone& channels = clip.bones[bone];

    if (channels.position.count > 0) {
        const KeyPair k = locateKeys(clip.positionTimes, channels.position, time);
        local.position = glm::mix(clip.positions[k.a], clip.positions[k.b], k.t);
    }
    if (channels.rotation.count > 0) {
        const KeyPair k = locateKeys(clip.rotationTimes, channels.rotation, time);
        local.rotation = glm::slerp(clip.rotations[k.a], clip.rotations[k.b], k.t);
    }
    if (channels.scale.count > 0) {
        const KeyPair k = locateKeys(clip.scaleTimes, channels.scale, time);
        local.scale = glm::mix(clip.scales[k.a], clip.scales[k.b], k.t);
    }
}

/**
 * @brief Apply @p adjust to @p local, the bone's transform in its parent's frame.
 *
 * The model-space adjustment is brought through the parent's composed matrix: the rotation conjugated
 * by the parent's rotation, the offset through the inverse of its upper 3x3 so a scaled parent does not
 * scale the meter. Exact under uniform scale; non-uniform scale turns about a slightly sheared axis.
 *
 * @param local The bone's local TRS, adjusted in place.
 * @param adjust What to add, or what to set.
 * @param parentGlobal The parent's composed model matrix, or null for a root.
 */
void adjustBone(Transform& local, const BoneAdjust& adjust, const glm::mat4* parentGlobal) {
    local.scale *= adjust.scale;
    if (!parentGlobal) {
        local.rotation = adjust.absolute ? adjust.rotation : adjust.rotation * local.rotation;
        local.position += adjust.offset;
        return;
    }

    const glm::quat parentRotation = Math::worldRotationOf(*parentGlobal);
    local.rotation = adjust.absolute
        ? glm::inverse(parentRotation) * adjust.rotation
        : glm::inverse(parentRotation) * adjust.rotation * parentRotation * local.rotation;
    // A parent scaled to nothing holds its children at its origin, whatever the offset.
    const glm::mat3 parentAxes(*parentGlobal);
    if (glm::determinant(parentAxes) != 0.0f) local.position += glm::inverse(parentAxes) * adjust.offset;
}

} // namespace

PoseSample sampleAhead(
    const Animator& animator,
    const AnimationClipAsset* clip,
    const AnimationClipAsset* from,
    float seconds
) {
    PlaybackHeads heads = PlaybackHeads::of(animator);
    if (seconds > 0.0f) {
        advanceHeads(animator, heads, clip ? clip->duration : 0.0f, from ? from->duration : 0.0f, seconds);
    }

    PoseSample sample;
    sample.clip        = clip;
    sample.time        = heads.time;
    sample.from        = from;
    sample.fromTime    = heads.fadeTime;
    sample.adjust      = animator.adjust.data();
    sample.adjustCount = static_cast<uint32_t>(animator.adjust.size());
    // A spent fade is all the playing clip.
    sample.weight = (animator.fadeDuration > 0.0f && heads.fadeRemaining > 0.0f)
        ? 1.0f - heads.fadeRemaining / animator.fadeDuration
        : 1.0f;
    return sample;
}

void composePose(const SkeletonAsset& skeleton, const PoseSample& sample, const PoseWrite& out) {
    const auto count = static_cast<uint32_t>(skeleton.bones.size());

    const float weight  = std::clamp(sample.weight, 0.0f, 1.0f);
    const bool blending = sample.from && weight < 1.0f;

    for (uint32_t i = 0; i < count; ++i) {
        Transform local = Transform::fromPose(skeleton.bindPose[i]);
        if (sample.clip) sampleBone(*sample.clip, i, sample.time, local);

        if (blending) {
            Transform leaving = Transform::fromPose(skeleton.bindPose[i]);
            sampleBone(*sample.from, i, sample.fromTime, leaving);
            local.position = glm::mix(leaving.position, local.position, weight);
            local.rotation = glm::slerp(leaving.rotation, local.rotation, weight);
            local.scale    = glm::mix(leaving.scale, local.scale, weight);
        }

        const int32_t parent = skeleton.bones[i].parent;

        // Linear: the list is a handful of bones, in any order.
        for (uint32_t a = 0; a < sample.adjustCount; ++a) {
            if (sample.adjust[a].bone != static_cast<int32_t>(i)) continue;
            adjustBone(local, sample.adjust[a], parent < 0 ? nullptr : &out.global[parent]);
        }

        const glm::mat4 bone = Transform::computeModelMatrix(local);
        out.global[i]  = (parent < 0) ? bone : out.global[parent] * bone;
        out.palette[i] = out.global[i] * skeleton.inverseBind[i];
    }

    finishSlice(out);
}

} // namespace Vkm::Engine
