#include "system/animation/pose_evaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "ecs/component/core/transform.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief @p clip if its per-bone table is parallel to @p skeleton's, else null.
 *
 * A clip cooked against another rig would pose the wrong joints out of matching
 * indices, or index past its own end. Asked of each clip separately, so a bad
 * outgoing clip cannot take the incoming one down with it.
 *
 * @param clip Clip to check, or null.
 * @param skeleton Rig being posed.
 * @return The clip, or nullptr when it does not belong to this rig.
 */
const AnimationClipAsset* boundTo(const AnimationClipAsset* clip, const SkeletonAsset& skeleton) {
    return (clip && clip->bones.size() == skeleton.bones.size()) ? clip : nullptr;
}

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

/**
 * @brief Move one playback head on and bring it back into the clip's range.
 *
 * Wrapping is a floor-subtract rather than a modulo because a negative speed has
 * to come round to the end of the clip, and fmod of a negative time stays
 * negative.
 *
 * @param time Head to advance, in place.
 * @param duration Clip length in seconds; 0 disables wrapping entirely.
 * @param delta Seconds to advance by, already scaled by the playback speed.
 * @param looping Whether the clip wraps rather than stopping at its end.
 * @return False when a non-looping clip has run out - which only the animator's
 *         own head acts on.
 */
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

} // namespace

void advancePlayback(Animator& animator, float duration, float fromDuration, float simDelta) {
    if (simDelta <= 0.0f) return;

    const float delta = simDelta * animator.speed;
    if (animator.playing && !advanceHead(animator.time, duration, delta, animator.looping)) {
        animator.playing = false;
    }

    // Deliberately not gated on `playing`. A one-shot clip that runs out in the
    // middle of a blend into it stops its own head, and a fade that stopped with
    // it would hold the character at a weight no field names and nothing clears
    // - visible as mostly the clip it already left. The blend is about reaching
    // the clip, not about that clip advancing.
    if (animator.fadeRemaining <= 0.0f) return;

    // The outgoing clip keeps playing while it fades, so the blend is between
    // two moving poses. It never stops the animator: what is playing is the clip
    // that was faded to, and an outgoing one that runs out holds its last frame
    // for the rest of the blend.
    advanceHead(animator.fadeTime, fromDuration, delta, animator.looping);

    // Unscaled by speed: a blend length is a duration the caller asked for, not
    // one the playback rate moves under them.
    animator.fadeRemaining -= simDelta;
    if (animator.fadeRemaining > 0.0f) return;

    animator.fadeFrom      = {};
    animator.fadeTime      = 0.0f;
    animator.fadeRemaining = 0.0f;
    animator.fadeDuration  = 0.0f;
}

void composePose(
    const SkeletonAsset& skeleton,
    const PoseSample& sample,
    const PoseWrite& out
) {
    const auto count = static_cast<uint32_t>(skeleton.bones.size());
    const AnimationClipAsset* bound = boundTo(sample.clip, skeleton);
    const AnimationClipAsset* from  = boundTo(sample.from, skeleton);

    // A weight of 1 is the whole of the incoming clip, which is every frame that
    // is not mid-fade - so the second sample and the three interpolations below
    // cost nothing at all until a crossfade is actually running.
    const float weight  = std::clamp(sample.weight, 0.0f, 1.0f);
    const bool blending = from && weight < 1.0f;

    glm::vec3 originMin(std::numeric_limits<float>::max());
    glm::vec3 originMax(std::numeric_limits<float>::lowest());
    float maxScale = 1.0f;

    for (uint32_t i = 0; i < count; ++i) {
        Transform local = skeleton.bindPose[i];
        if (bound) sampleBone(*bound, i, sample.time, local);

        if (blending) {
            Transform leaving = skeleton.bindPose[i];
            sampleBone(*from, i, sample.fromTime, leaving);
            // In LOCAL space, before composition. Blending the composed matrices
            // instead pulls a joint toward the midpoint of two world positions,
            // which shortens the limb it hangs off.
            local.position = glm::mix(leaving.position, local.position, weight);
            local.rotation = glm::slerp(leaving.rotation, local.rotation, weight);
            local.scale    = glm::mix(leaving.scale, local.scale, weight);
        }

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
