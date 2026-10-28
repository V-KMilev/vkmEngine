#pragma once

#include "core/reflect.h"

#include "resource/asset/audio_clip_asset.h"

namespace Vkm::Engine {

/**
 * @brief How a positioned source fades between minDistance and maxDistance.
 */
enum class AudioRolloff {
    Linear  = 0,  ///< A straight line from full volume to silence.
    Inverse = 1,  ///< As 1/distance, steep near and gentle far, bent to reach silence at maxDistance.
    Count         ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief A sound emitter: one clip, how loud, and where it is heard from.
 *
 * `playing` is the state the source wants, not a device report; it reads back false
 * when a one-shot finishes. One sound per source, so overlapping copies want a source
 * each; the playback cursor is read via AudioDevice::voiceCursor. A spatial source
 * wants a mono clip (docs/reference/audio.md, "A positioned source wants a mono clip").
 */
struct AudioSource {
    AudioClipHandle clip;

    float volume = 1.0f;   ///< Linear gain, multiplied into the listener's own.
    float pitch  = 1.0f;   ///< Playback rate multiplier; also shifts the pitch. Must be > 0.
    bool  loop   = false;  ///< Restart at the end instead of finishing.

    /**
     * @brief Whether the source is positioned in the world or mixed flat at constant volume.
     */
    bool spatial = true;

    /**
     * @brief Start playing on the first frame the simulation runs.
     *
     * Gated on simulation time, not on the source existing, so the editor stays quiet.
     * See engine.md, "Authored state and session state on one component".
     */
    bool playOnStart = true;

    /**
     * @brief Radius inside which the source is at full `volume`: the size of the emitter.
     */
    float minDistance = 1.0f;

    /**
     * @brief Distance at which the source is inaudible.
     *
     * Attenuation follows `rolloff` between the two; at or below minDistance disables it.
     */
    float maxDistance = 50.0f;

    AudioRolloff rolloff = AudioRolloff::Linear;  ///< How it fades between the two distances.

    /**
     * @brief Whether the world's colliders shape it: muffled behind a wall, ringing in a room.
     *
     * A positioned source only, and only while the scene's Environment::acoustics is on. Off for a
     * sound that must cut through, as a voice in the player's ear.
     */
    bool acoustics = true;

    /**
     * @brief Which voice gives way when too many sounds play (AudioDevice::play).
     *
     * A source whose voice was taken reads `playing` false, as one that finished.
     */
    int priority = 0;

    bool playing = false;  ///< Session state.
    bool started = false;  ///< Whether playOnStart has been honoured; session state.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::AudioRolloff, "Linear", "Inverse")

VKM_REFLECT_BEGIN(::Vkm::Engine::AudioSource)
    VKM_F(clip)
    VKM_F(volume)
    VKM_F(pitch)
    VKM_F(loop)
    VKM_F(spatial)
    VKM_F(playOnStart)
    VKM_F(minDistance)
    VKM_F(maxDistance)
    VKM_F(rolloff)
    VKM_F(acoustics)
    VKM_F(priority)
VKM_REFLECT_END()
