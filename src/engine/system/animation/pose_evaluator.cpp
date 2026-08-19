#include "system/animation/pose_evaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "ecs/component/transform.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

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

// Both ends clamp rather than extrapolate: a clip sampled before its first key
// or after its last holds that key, which is what makes a channel that covers
// only part of the timeline behave like a held pose instead of drifting.
KeyPair locateKeys(const std::vector<float>& times, const ClipChannel& channel, float time) {
    const auto begin = times.begin() + channel.first;
    const auto end   = begin + channel.count;

    const auto it = std::upper_bound(begin, end, time);
    if (it == begin) return {channel.first, channel.first, 0.0f};
    if (it == end) {
        const uint32_t last = channel.first + channel.count - 1;
        return {last, last, 0.0f};
    }

    const auto b = static_cast<uint32_t>(it - times.begin());
    const uint32_t a = b - 1;
    const float span = times[b] - times[a];
    return {a, b, span > 0.0f ? (time - times[a]) / span : 0.0f};
}

// Only the channels the clip actually holds are written, so a bone the clip has
// nothing to say about keeps the bind value it arrived with.
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

} // namespace

void advancePlayback(Animator& animator, float duration, float simDelta) {
    if (!animator.playing || simDelta <= 0.0f) return;

    animator.time += simDelta * animator.speed;
    if (duration <= 0.0f) return;

    if (animator.looping) {
        animator.time -= std::floor(animator.time / duration) * duration;
    } else if (animator.time >= duration) {
        animator.time    = duration;
        animator.playing = false;
    } else if (animator.time < 0.0f) {
        animator.time    = 0.0f;
        animator.playing = false;
    }
}

void composePose(
    const SkeletonAsset& skeleton,
    const AnimationClipAsset* clip,
    float time,
    const PoseWrite& out
) {
    const auto count = static_cast<uint32_t>(skeleton.bones.size());
    const AnimationClipAsset* bound =
        (clip && clip->bones.size() == skeleton.bones.size()) ? clip : nullptr;

    glm::vec3 originMin(std::numeric_limits<float>::max());
    glm::vec3 originMax(std::numeric_limits<float>::lowest());
    float maxScale = 1.0f;

    for (uint32_t i = 0; i < count; ++i) {
        Transform local = skeleton.bindPose[i];
        if (bound) sampleBone(*bound, i, time, local);

        const glm::mat4 bone = Transform::computeModelMatrix(local);
        const int32_t parent = skeleton.bones[i].parent;
        out.global[i]  = (parent < 0) ? bone : out.global[parent] * bone;
        out.palette[i] = out.global[i] * skeleton.inverseBind[i];

        const glm::vec3 origin(out.global[i][3]);
        originMin = glm::min(originMin, origin);
        originMax = glm::max(originMax, origin);

        // Accumulated, not local: a bone under a scaled parent carries that
        // scale too, and it is the total that stretches the skin. Floored at 1
        // because this only ever inflates a bounding box, and the occlusion
        // cull keeps conservatively - an under-sized box deletes geometry that
        // was visible, an over-sized one costs a draw.
        maxScale = std::max({maxScale,
            glm::length(glm::vec3(out.global[i][0])),
            glm::length(glm::vec3(out.global[i][1])),
            glm::length(glm::vec3(out.global[i][2]))});
    }

    if (count == 0) originMin = originMax = glm::vec3(0.0f);

    out.slice->originMin    = originMin;
    out.slice->originMax    = originMax;
    out.slice->maxBoneScale = maxScale;
}

} // namespace Vkm::Engine
