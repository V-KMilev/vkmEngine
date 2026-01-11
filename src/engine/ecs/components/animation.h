#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "animation_track.h"

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

    float time   = 0.0f;    ///< Current animation time in seconds
    float speed  = 1.0f;    ///< Playback speed multiplier (1.0 = normal, 2.0 = double speed, etc.)
    bool playing = true;    ///< Is animation currently playing?
    bool looping = true;    ///< Should animation loop when it reaches the end?
};

} // namespace Engine
