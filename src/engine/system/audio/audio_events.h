#pragma once

#include "resource/asset/audio_clip_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

/**
 * @brief A request to hear a clip once, from gameplay that owns no entity for it.
 *
 * AudioSource is the answer whenever there is a thing making the sound: it can
 * be stopped, moved, looped and read back. This is the answer when there is not
 * - a coin from a pool that recycles as it is collected, an impact on a body
 * about to be destroyed, or any sound that must overlap a copy of itself, which
 * one source cannot do.
 *
 *   context().events->emit(PlaySoundEvent{m_chime, params});
 *
 * It hands back no id, so nothing can stop it, move it or ask whether it
 * finished, and nothing has to own it: the voice plays to its end and the next
 * sweep of finished voices releases it. Which is why `loop` is ignored - a sound
 * with no end needs an id to stop it, so it needs a component - and why a
 * position here is a position rather than a follow.
 *
 * Emitted synchronously, so a request made during Simulation is heard in the
 * same frame. Requests carry no priority and share the mixer's voice budget with
 * every source in the scene; at the ceiling one is dropped rather than
 * displacing a voice already playing, and the mixer says so once.
 */
struct PlaySoundEvent {
    AudioClipHandle clip;
    VoiceParams     params;
};

} // namespace Vkm::Engine
