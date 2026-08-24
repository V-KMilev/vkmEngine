#pragma once

#include "resource/asset/audio_clip_asset.h"

namespace Vkm::Engine {

/**
 * @brief A sound emitter: one clip, how loud, and where it is heard from.
 *
 * AudioSystem reconciles this against the mixer every frame. `playing` is the
 * state the source WANTS to be in, not a report of what the device is doing:
 * gameplay sets it to start or stop a sound and reads it back to learn that a
 * one-shot has finished, which is why it is one field rather than a request and
 * a status that could disagree.
 *
 * At most one sound plays per source. A second `playing = true` while the first
 * is still going does nothing - the source is a speaker, not a queue - so
 * overlapping copies of the same footstep want a source each. That is the
 * simple shape, and it is the one the ECS already gives: an entity per voice.
 *
 * `spatial` decides whether the entity's world position is heard at all. A 3D
 * source is positioned and attenuated by distance to the listener; a 2D one is
 * mixed flat, which is what music, narration and UI clicks want. Spatializing a
 * stereo clip is meaningless (its two channels already encode a position), so a
 * spatial source wants a mono clip.
 */
struct AudioSource {
    AudioClipHandle clip;

    float volume = 1.0f;   ///< Linear gain, multiplied into the listener's own.
    float pitch  = 1.0f;   ///< Playback rate multiplier; also shifts the pitch. Must be > 0.
    bool  loop   = false;  ///< Restart at the end instead of finishing.

    /**
     * @brief Whether the source is positioned in the world or mixed flat.
     *
     * Turning this off is what makes a music bed or a UI click audible at a
     * constant volume no matter where its entity sits.
     */
    bool spatial = true;

    /**
     * @brief Start playing on the first frame the simulation runs.
     *
     * Deliberately gated on simulation time rather than on the source simply
     * existing: in the editor, an unplayed scene is a paused one, and a source
     * that started merely by being loaded would fill the editor with noise
     * nobody asked to hear. It is the same rule a behavior's onStart follows.
     */
    bool playOnStart = true;

    /**
     * @brief Distance at which the source is at full `volume`.
     *
     * Inside this radius nothing is attenuated, so it is the size of the thing
     * making the sound rather than a fade parameter.
     */
    float minDistance = 1.0f;

    /**
     * @brief Distance at which the source is inaudible.
     *
     * Attenuation is linear between the two distances, which is the model that
     * makes both numbers mean what an author expects: full here, silent there.
     * A maxDistance at or below minDistance disables attenuation entirely.
     */
    float maxDistance = 50.0f;

    /**
     * @brief Whether the source should be sounding right now.
     *
     * Runtime state, not serialized, for the same reason a behavior's started
     * flag is not: it describes a play session rather than the authored scene,
     * and a scene that came back from disk mid-sound would resume a noise whose
     * beginning nobody heard. `playOnStart` is the authored half.
     */
    bool playing = false;

    /**
     * @brief Whether playOnStart has already been honoured this session.
     *
     * Runtime state. Without it a one-shot with playOnStart would restart every
     * frame after it ended, since `playing` falling back to false is exactly
     * what "it finished" looks like.
     */
    bool started = false;
};

} // namespace Vkm::Engine
