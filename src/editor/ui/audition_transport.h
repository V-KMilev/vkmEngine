#pragma once

#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct AudioClipAsset;

/**
 * @brief Draw one clip audition's transport: Play / Pause / Resume, then Stop.
 *
 * The glyph is what pressing does next. Stop is lit off the device, since a one-shot
 * ends untold. Flat, not spatial: there may be no listener. Never touches the scene,
 * where setting `playing` would be an undoable edit.
 *
 * @param idStr Unique id fragment for both buttons.
 * @param device Plays the audition; a closed one disables the transport and says why.
 * @param voice Replaced by Play (releasing any previous audition, even another row's),
 *        cleared by Stop.
 * @param mine Whether @p voice belongs to this card or row; false offers Play alone.
 * @param clip Clip Play auditions; null leaves Play with nothing to start.
 * @param size Button side length in pixels.
 * @return True on the frame Play started a new audition, so the caller can record its owner.
 */
bool auditionTransport(
    const char* idStr,
    AudioDevice& device,
    VoiceId& voice,
    bool mine,
    const AudioClipAsset* clip,
    float size
);

/**
 * @brief Draw an audition's position slider, measured against its clip.
 *
 * The cursor is read off the device, which the mixer advances; see AudioDevice::voiceCursor.
 *
 * @param idStr Unique id fragment; the slider's label is hidden.
 * @param device Device holding the cursor.
 * @param voice The audition to scrub; 0 (not this card's or row's) disables the slider.
 * @param duration Clip length in seconds; zero or less draws nothing.
 * @param width Slider width in pixels; -1 fills the remaining row.
 */
void auditionScrubber(const char* idStr, AudioDevice& device, VoiceId voice, float duration, float width);

} // namespace Vkm::Engine
