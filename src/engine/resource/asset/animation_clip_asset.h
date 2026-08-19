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
 * @brief A baked animation: every bone's keys, in six flat arrays.
 *
 * `AnimationTrack<T>` is deliberately not reused here. Three tracks over a
 * hundred bones is three hundred heap vector pairs and three hundred easing
 * function pointers for one clip, where six flat arrays are six allocations,
 * bulk-writable to the cooked file and cache-linear over a bone sweep. Easing
 * is dropped with it: keys come out of a DCC tool already baked at its own
 * sample rate, and there is no author to pick a curve per bone. The keyframe
 * `Animation` component keeps `AnimationTrack<T>` and is untouched.
 *
 * A clip is bound to its rig at cook time - `bones` is parallel to the named
 * skeleton's bone array, so nothing resolves names at runtime.
 */
struct AnimationClipAsset : public Resource {
    std::string skeleton;         ///< Name of the rig whose bone order `bones` addresses.

    /**
     * @brief Length in seconds.
     *
     * Stored rather than derived from the last key, so a clip that ends on a
     * held pose keeps the still tail its author gave it.
     */
    float duration = 0.0f;

    std::vector<ClipBone> bones;  ///< Parallel to the skeleton's bones.

    std::vector<float>     positionTimes;
    std::vector<glm::vec3> positions;
    std::vector<float>     rotationTimes;
    std::vector<glm::quat> rotations;
    std::vector<float>     scaleTimes;
    std::vector<glm::vec3> scales;
};

using AnimationClipHandle = Handle<AnimationClipAsset>;

} // namespace Vkm::Engine
