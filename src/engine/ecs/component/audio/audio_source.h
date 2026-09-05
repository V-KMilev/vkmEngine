#pragma once

#include "core/reflect.h"

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
 * At most one sound plays per source - the source is a speaker, not a queue - so
 * overlapping copies of one footstep want a source each, which is the shape the
 * ECS already gives. There is no playback position here either: the cursor is
 * advanced by the mixer thread between frames, and AudioDevice::voiceCursor is
 * where it is read.
 *
 * `spatial` decides whether the entity's world position is heard at all, and a
 * spatial source wants a MONO clip: the mixer routes each channel to the output
 * it was authored for, so half a stereo clip's field is unreachable wherever the
 * emitter is put. docs/reference/system/audio.md, "A positioned source wants a
 * mono clip", has the measurements.
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
     * The authored half of the trio every component that plays something
     * carries; see engine.md, "Authored state and session state on one
     * component". Gated on simulation time rather than on the source existing,
     * or the editor would fill with noise nobody asked to hear.
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

    /// Whether the source should be sounding right now. Session state.
    bool playing = false;

    /// Whether playOnStart has been honoured yet this session. Session state.
    bool started = false;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::AudioSource)
    VKM_F(clip),
    VKM_F(volume),
    VKM_F(pitch),
    VKM_F(loop),
    VKM_F(spatial),
    VKM_F(playOnStart),
    VKM_F(minDistance),
    VKM_F(maxDistance)
VKM_REFLECT_END()
