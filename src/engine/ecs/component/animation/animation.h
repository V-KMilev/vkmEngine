#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/animation/animation_track.h"

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
    float time    = 0.0f;     ///< Current animation time in seconds
    float speed   = 1.0f;     ///< Playback speed multiplier
    bool  looping = true;

    /**
     * @brief Start playing on the first frame the simulation runs.
     *
     * The authored half, the way AudioSource splits the same pair. Gated on
     * simulation time rather than on the component existing: in the editor an
     * unplayed scene is a paused one, and an animation that ran merely because
     * it was loaded would move the entity the author is placing.
     */
    bool playOnStart = true;

    /**
     * @brief Whether the animation should be advancing right now.
     *
     * Runtime state, not serialized, for the same reason AudioSource::playing
     * is not: it describes a play session rather than the authored scene. The
     * editor's transport writes it to preview, and a preview left running is
     * not a decision about what a shipped scene does - `playOnStart` is.
     */
    bool playing = false;

    /**
     * @brief Whether playOnStart has already been honoured this session.
     *
     * Runtime state. Without it a non-looping clip with playOnStart would
     * restart every frame after it ended, since `playing` falling back to false
     * is exactly what "it finished" looks like.
     */
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
