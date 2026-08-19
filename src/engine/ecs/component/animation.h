#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/animation/animation_track.h"

namespace Engine {

/**
 * @brief Component representing an animation that can be applied to an entity.
 *
 * Drives the position, rotation and scale of the entity's Transform.
 */
struct Animation {
    AnimationTrack<glm::vec3> positionTrack;
    AnimationTrack<glm::quat> rotationTrack;
    AnimationTrack<glm::vec3> scaleTrack;

    float length  = 0.0f;     ///< Explicit minimum length in seconds (0 = auto from last keyframe)
    float time    = 0.0f;     ///< Current animation time in seconds
    float speed   = 1.0f;     ///< Playback speed multiplier
    bool  playing = false;
    bool  looping = true;

    /**
     * @brief The animation's effective length: the latest keyframe across all
     *        three tracks, or the explicit @ref length, whichever is greater.
     */
    static float computeDuration(const Animation& animation) {
        return std::max({
            animation.positionTrack.getDuration(),
            animation.rotationTrack.getDuration(),
            animation.scaleTrack.getDuration(),
            animation.length
        });
    }
};

} // namespace Engine
