#pragma once

#include "resource/asset/audio_clip_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

/**
 * @brief A request to hear a clip once, from gameplay that owns no entity for it.
 *
 * AudioSource is the answer whenever there is a thing making the sound: it can
 * be stopped, moved, looped and read back. This is the answer when there is
 * not - a coin from a pool that recycles the instant it is collected, an
 * impact on a body about to be destroyed, or any sound that must overlap a
 * copy of itself, which one source cannot do because a source is a speaker
 * rather than a queue.
 *
 *   context().events->emit(PlaySoundEvent{m_chime, params});
 *
 * What makes it safe to forget is that it hands back no id: nothing can stop
 * it, move it or ask whether it finished, so nothing has to own it. The voice
 * is not entered in AudioSystem's table - it plays to its end and the next
 * frame's sweep of finished voices releases it. Which is also why `loop` is
 * ignored: a sound with no end needs an id to stop it, so it needs a
 * component. Everything else in VoiceParams applies, position included, but
 * it is a position rather than a follow - a request is fixed where it was
 * made, and a sound that must travel with a moving emitter wants a source.
 *
 * Emitted synchronously, so a request made during Simulation is heard in the
 * same frame's Transform stage, exactly as writing AudioSource::playing is.
 * Requests carry no priority and share the mixer's uncapped voice budget with
 * every source in the scene.
 */
struct PlaySoundEvent {
    AudioClipHandle clip;
    VoiceParams     params;
};

} // namespace Vkm::Engine
