#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/animation/animation_track.h"

namespace Vkm::Engine {

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
    /// Playback head in seconds. Session state; see Animator::time for why it
    /// is not serialized.
    float time    = 0.0f;
    float speed   = 1.0f;     ///< Playback speed multiplier
    bool  looping = true;

    /**
     * @brief Start playing on the first frame the simulation runs.
     *
     * The authored half of the trio every component that plays something
     * carries; see engine.md, "Authored state and session state on one
     * component", for why only this one is serialized.
     */
    bool playOnStart = true;

    /// Whether the animation should be advancing right now. Session state.
    bool playing = false;

    /// Whether playOnStart has been honoured yet this session. Session state.
    bool started = false;

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

} // namespace Vkm::Engine
