#include "system/animation/pose_evaluator.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "ecs/component/core/transform.h"
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

    const auto b = static_cast<uint32_t>(it - times.begin());
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
 * scale the metre. Exact under uniform scale; non-uniform scale turns about a slightly sheared axis.
 *
 * @param local The bone's local TRS, adjusted in place.
 * @param adjust What to add, or what to set.
 * @param parentGlobal The parent's composed model matrix, or null for a root.
 */
void adjustBone(Transform& local, const BoneAdjust& adjust, const glm::mat4* parentGlobal) {
    local.scale *= adjust.scale;
    if (!parentGlobal) {
        local.rotation  = adjust.absolute ? adjust.rotation : adjust.rotation * local.rotation;
        local.position += adjust.offset;
        return;
    }

    const glm::mat3 parentAxes(*parentGlobal);
    const glm::mat3 parentBasis(
        glm::normalize(parentAxes[0]),
        glm::normalize(parentAxes[1]),
        glm::normalize(parentAxes[2])
    );
    const glm::quat parentRotation = glm::normalize(glm::quat_cast(parentBasis));

    local.rotation = adjust.absolute
        ? glm::inverse(parentRotation) * adjust.rotation
        : glm::inverse(parentRotation) * adjust.rotation * parentRotation * local.rotation;
    local.position += glm::inverse(parentAxes) * adjust.offset;
}

} // namespace

bool advanceHead(float& time, float duration, float delta, bool looping) {
    time += delta;
    if (duration <= 0.0f) return true;

    if (looping) {
        time -= std::floor(time / duration) * duration;
    } else if (time >= duration) {
        time = duration;
        return false;
    } else if (time < 0.0f) {
        time = 0.0f;
        return false;
    }
    return true;
}

void rewindSpentHead(float& time, float duration, float delta, bool looping) {
    if (looping || duration <= 0.0f) return;
    if (delta < 0.0f && time <= 0.0f) {
        time = duration;
    } else if (delta > 0.0f && time >= duration) {
        time = 0.0f;
    }
}

PlaybackStep advancePlayback(Animator& animator, float duration, float fromDuration, float simDelta) {
    if (simDelta <= 0.0f) return PlaybackStep{animator.time, animator.time, 0.0f};

    if (animator.playOnStart && !animator.started) {
        animator.started = true;
        animator.playing = true;
    }

    // Rewound before the sweep is measured, so it starts where playing starts.
    const float delta = simDelta * animator.speed;
    if (animator.playing) rewindSpentHead(animator.time, duration, delta, animator.looping);

    PlaybackStep step{animator.time, animator.time, 0.0f};
    if (animator.playing) {
        if (!advanceHead(animator.time, duration, delta, animator.looping)) animator.playing = false;
        step.to = animator.time;
        // Subtracting misreads a wrapped head: a loop covers what was asked, a clamp what it allowed.
        step.travel = animator.looping ? delta : animator.time - step.from;
    }

    // Not gated on `playing`: a one-shot ending mid-blend would strand the fade at a weight no field names.
    if (animator.fadeRemaining <= 0.0f) return step;

    advanceHead(animator.fadeTime, fromDuration, delta, animator.fadeLooping);

    animator.fadeRemaining -= simDelta;
    if (animator.fadeRemaining > 0.0f) return step;

    animator.fadeFrom      = {};
    animator.fadeTime      = 0.0f;
    animator.fadeRemaining = 0.0f;
    animator.fadeDuration  = 0.0f;
    return step;
}

bool crossesMarker(const PlaybackStep& step, float marker, float duration) {
    if (step.travel == 0.0f || duration <= 0.0f) return false;

    if (std::abs(step.travel) >= duration) return true;

    // Otherwise the wrap branches read a sub-resolution move as a full lap.
    if (step.from == step.to) return false;

    if (step.travel > 0.0f) {
        return (step.from < step.to)
            ? (marker > step.from && marker <= step.to)   // straight through
            : (marker > step.from || marker <= step.to);  // wrapped past the end
    }
    return (step.to < step.from)
        ? (marker >= step.to && marker < step.from)
        : (marker >= step.to || marker < step.from);      // wrapped past the start
}

void composePose(const SkeletonAsset& skeleton, const PoseSample& sample, const PoseWrite& out) {
    const auto count = static_cast<uint32_t>(skeleton.bones.size());

    const float weight  = std::clamp(sample.weight, 0.0f, 1.0f);
    const bool blending = sample.from && weight < 1.0f;

    for (uint32_t i = 0; i < count; ++i) {
        Transform local = skeleton.bindPose[i];
        if (sample.clip) sampleBone(*sample.clip, i, sample.time, local);

        if (blending) {
            Transform leaving = skeleton.bindPose[i];
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
