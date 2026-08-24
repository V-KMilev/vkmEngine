#pragma once

#include "system/audio/audio_device.h"

namespace Vkm::Engine {

struct AudioClipAsset;

/**
 * @brief Draw one clip audition's transport: Play / Pause / Resume, then Stop.
 *
 * The Inspector's Audio Source card and the Asset Browser's Sounds rows are
 * both asking to hear a clip, so they ask with one widget rather than two
 * vocabularies that drift apart. One button carries all three states, the way
 * both animation cards do it - the glyph is what pressing it will do next -
 * and Stop sits beside it, lit off the device rather than off a remembered id,
 * because an id outlives the voice it named: a one-shot ends on its own and
 * nothing here is told.
 *
 * The audition is flat rather than positioned. A default VoiceParams is
 * spatial, and a spatial voice is measured against the scene's listener: in a
 * project that has none yet - which is exactly the project someone is
 * importing sounds into - it is silent, and in one that has an ear somewhere
 * it plays at whatever the world origin sounds like from there. Asking to hear
 * a file is not asking to hear it from anywhere.
 *
 * It never touches the scene. Setting a source's `playing` flag instead would
 * be an edit - undoable, dirtying, and audible again on the next Play - when
 * all that was asked for was to hear the file. That is the rule both animation
 * cards follow: dirtying a scene every time somebody listens to something
 * would make the unsaved-changes prompt mean nothing.
 *
 * @param idStr Unique id fragment; both buttons derive their ids from it.
 * @param device Device the audition plays on. A closed one leaves the whole
 *        transport disabled and says why, rather than answering a press with
 *        silence.
 * @param voice The caller's audition voice: replaced by Play, cleared by Stop.
 *        Play releases whatever it held first, so a second audition replaces
 *        the first rather than layering a copy over it - including when the
 *        voice belongs to another row.
 * @param mine Whether @p voice is the audition of the card or row being drawn.
 *        False offers Play alone, so a row can never hold or cut short a sound
 *        it is not showing.
 * @param clip Clip Play auditions; null leaves Play with nothing to start.
 * @param size Button side length in pixels.
 * @return True on the frame Play started a new audition, so the caller can
 *         record which card or row the voice now belongs to.
 */
bool auditionTransport(const char* idStr, AudioDevice& device, VoiceId& voice,
                       bool mine, const AudioClipAsset* clip, float size);

/**
 * @brief Draw an audition's position slider, measured against its clip.
 *
 * Reads the cursor off the device rather than off a field on the component,
 * because the mixer advances it between our frames; AudioDevice::voiceCursor
 * carries the reasoning. With no voice there is no cursor, so the slider is
 * disabled rather than inventing a start offset that would live in a panel and
 * be forgotten the moment the selection moved.
 *
 * @param idStr Unique id fragment; the slider's label is hidden.
 * @param device Device holding the cursor.
 * @param voice The audition to scrub; pass 0 when the caller's voice is not
 *        this card's or row's, which disables the slider like a finished one.
 * @param duration Clip length in seconds; zero or less draws nothing, since a
 *        slider with no length has nothing to say.
 * @param width Slider width in pixels; -1 fills the remaining row.
 */
void auditionScrubber(const char* idStr, AudioDevice& device, VoiceId voice,
                      float duration, float width);

} // namespace Vkm::Engine
