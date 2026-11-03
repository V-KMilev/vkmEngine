#pragma once

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

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
 * each; the playback cursor is read via Audio::Device::voiceCursor. A spatial source
 * wants a mono clip (docs/reference/audio.md, "A positioned source wants a mono clip").
 */
struct AudioSource {
    AudioClipHandle clip;

    float volume = 1.0f;   ///< Linear gain, multiplied into the listener's own.
    float pitch  = 1.0f;   ///< Playback rate multiplier; also shifts the pitch. Floored at 0.01.
    bool  loop   = false;  ///< Restart at the end instead of finishing.

    bool spatial = true;  ///< Whether the source is positioned in the world or mixed flat at constant volume.

    bool playOnStart = true;  ///< Start playing on the first frame the simulation runs. Gated on simulation
                              ///< time, not on the source existing, so the editor stays quiet. See
                              ///< decisions.md, "Authored state and session state on one component".

    float minDistance = 1.0f;  ///< Radius inside which the source is at full `volume`: the size of the
                               ///< emitter.

    float maxDistance = 50.0f;  ///< Distance at which the source is inaudible; attenuation follows `rolloff`
                                ///< between the two, and at or below minDistance there is none. A source that
                                ///< starts out of range still starts, because it can move; a one-shot
                                ///< PlaySoundEvent out of range is dropped instead.

    AudioRolloff rolloff = AudioRolloff::Linear;  ///< How it fades between the two distances.

    bool acoustics = true;  ///< Whether the world's colliders shape it: muffled behind a wall, ringing in a
                            ///< room. A positioned source only, while the scene's Environment::acoustics is
                            ///< on; off for a sound that must cut through, as a voice in the player's ear.

    int priority = 0;  ///< Which voice gives way when too many sounds play (Audio::Device::play). A source
                       ///< whose voice was taken reads `playing` false, as one that finished.

    bool playing = false;  ///< Session state.
    bool started = false;  ///< Whether playOnStart has been honored; session state.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::AudioRolloff, "Linear", "Inverse")

VKM_REFLECT_BEGIN(::Vkm::Engine::AudioSource)
    VKM_F(clip, "The sound it plays")
    VKM_F(volume, 0.0f, 2.0f, "Gain, multiplied into the listener's")
    VKM_F(pitch, 0.1f, 4.0f, "Playback rate; it shifts the pitch too")
    VKM_F(loop, "Starts over at the end")
    VKM_F(spatial, "Placed in the world, fading with distance; off for music and UI")
    VKM_F(playOnStart, "Starts by itself on Play, never while a scene is only open")
    VKM_F(minDistance, 0.0f, 1000.0f, "Full volume inside this radius")
    VKM_F(maxDistance, 0.0f, 5000.0f, "Silent at this radius")
    VKM_F(rolloff, "Between the two: Linear straight, Inverse steep near the source")
    VKM_F(acoustics, "Shaped by the colliders: muffled behind a wall, ringing in a room")
    VKM_F(priority, -100.0f, 100.0f, "When too many play, the lower priority gives way")
VKM_REFLECT_END()
