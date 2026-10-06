#pragma once

#include <glm/glm.hpp>

#include "resource/asset/audio_clip_asset.h"
#include "system/audio/audio_device.h"

namespace Vkm::Engine {

/**
 * @brief A request to hear a clip once, from gameplay that owns no entity for it.
 *
 * For when no AudioSource fits: a sound on an entity about to go, or one that must
 * overlap itself. There is no id, so it cannot be stopped or moved; `loop` is ignored.
 * A request made during Simulation is heard the same frame; at the voice ceiling it
 * is dropped.
 *
 *   events().emit(PlaySoundEvent::at(m_chime, transform->position));
 */
struct PlaySoundEvent {
    AudioClipHandle clip;
    VoiceParams     params;

    /**
     * @brief A request to hear @p clip once, from @p position.
     *
     * @param clip     Clip to play; an invalid handle is heard as nothing.
     * @param position Where it sounds from, in world space.
     * @param volume   Linear gain, multiplied into the master.
     * @return The request, spatial, with every other parameter at its default.
     */
    static PlaySoundEvent at(AudioClipHandle clip, const glm::vec3& position, float volume = 1.0f) {
        PlaySoundEvent event{clip, {}};
        event.params.position = position;
        event.params.volume   = volume;
        return event;
    }
};

} // namespace Vkm::Engine
