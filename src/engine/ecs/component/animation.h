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

    float duration = 0.0f;    ///< Cached duration of the animation in seconds (longest track)
    float time     = 0.0f;    ///< Current animation time in seconds
    float speed    = 1.0f;    ///< Playback speed multiplier (1.0 = normal, 2.0 = double speed, etc.)
    bool playing   = true;    ///< Is animation currently playing?
    bool looping   = true;    ///< Should animation loop when it reaches the end?

    /**
     * @brief Updates the cached duration from the longest track.
     * Call this after modifying keyframes or tracks.
     */
    void updateDuration() {
        duration = std::max({
            positionTrack.getDuration(),
            rotationTrack.getDuration(),
            scaleTrack.getDuration()
        });
    }
};

} // namespace Engine
