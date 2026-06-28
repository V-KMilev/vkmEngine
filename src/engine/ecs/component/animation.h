#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/animation/animation_track.h"

namespace Engine {

/**
 * @brief Component representing an animation that can be applied to an entity.
 *
 * Simple data-only component. Supports animating position, rotation, and scale of Transform components.
 * Animation updates and application should be handled by systems that process this component.
 */
struct Animation {
    AnimationTrack<glm::vec3> positionTrack;    ///< Position animation track
    AnimationTrack<glm::quat> rotationTrack;    ///< Rotation animation track
    AnimationTrack<glm::vec3> scaleTrack;       ///< Scale animation track

    float duration = 0.0f;     ///< Cached effective duration in seconds. Stale until updateDuration() is called after a track/length edit.
    float length   = 0.0f;     ///< Explicit minimum length in seconds (0 = auto from last keyframe)
    float time     = 0.0f;     ///< Current animation time in seconds
    float speed    = 1.0f;     ///< Playback speed multiplier (1.0 = normal, 2.0 = double speed, etc.)
    bool  playing  = false;    ///< Is animation currently playing?
    bool  looping  = true;     ///< Should animation loop when it reaches the end?

    /**
     * @brief Recomputes the cached duration as the maximum of every track's
     * last keyframe and the explicit @ref length. Call after modifying
     * keyframes, tracks, or length.
     */
    void updateDuration() {
        duration = std::max({
            positionTrack.getDuration(),
            rotationTrack.getDuration(),
            scaleTrack.getDuration(),
            length
        });
    }
};

} // namespace Engine
