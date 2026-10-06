#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

/**
 * @brief One bone's keys for one channel, as a range into the clip's flat arrays.
 */
struct ClipChannel {
    uint32_t first = 0;
    uint32_t count = 0;  ///< 0 means the channel is absent and the bind pose stands.
};

/**
 * @brief The three channel ranges belonging to one bone of the clip's skeleton.
 */
struct ClipBone {
    ClipChannel position;
    ClipChannel rotation;
    ClipChannel scale;
};

/**
 * @brief A named instant a clip announces as its playback head reaches it.
 *
 * The name is the whole identity (as BoneSocket's bone name is); nothing
 * addresses a marker by position, so two may share a name.
 */
struct ClipMarker {
    std::string name;
    float       time = 0.0f;  ///< Seconds into the clip; within [0, duration].
};

/**
 * @brief A baked animation: every bone's keys, in six flat arrays.
 *
 * Not `AnimationTrack<T>`: six flat arrays are bulk-writable and cache-linear,
 * and baked keys need no easing. `bones` is parallel to the named skeleton's
 * bone array, bound at cook time.
 */
struct AnimationClipAsset : public Resource {
    std::string skeleton;         ///< Name of the rig whose bone order `bones` addresses.

    /**
     * @brief Length in seconds.
     *
     * Stored, not derived from the last key, so a held tail is kept.
     */
    float duration = 0.0f;

    std::vector<ClipBone> bones;  ///< Parallel to the skeleton's bones.

    /**
     * @brief Instants the clip announces as the head passes them, in time order.
     *
     * On the clip, not the Animator: every rig playing the walk gets its footsteps.
     */
    std::vector<ClipMarker> markers;

    std::vector<float>     positionTimes;
    std::vector<glm::vec3> positions;
    std::vector<float>     rotationTimes;
    std::vector<glm::quat> rotations;
    std::vector<float>     scaleTimes;
    std::vector<glm::vec3> scales;
};

/**
 * @brief Why @p clip cannot be played, or an empty string when it can.
 *
 * What the sampler relies on unchecked: a finite duration, paired key times and
 * values, channels in range, markers on the timeline. Rig fit is not asked.
 *
 * @param clip Clip to judge.
 * @return The first fault found, worded to follow "cannot be played - ".
 */
std::string findClipFault(const AnimationClipAsset& clip);

using AnimationClipHandle = Handle<AnimationClipAsset>;

} // namespace Vkm::Engine
