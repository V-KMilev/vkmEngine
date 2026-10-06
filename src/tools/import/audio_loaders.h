#pragma once

#include <string>

#include "resource/asset/audio_clip_asset.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Import a sound file into a fully decoded AudioClipAsset.
 *
 * Decodes wav / mp3 / flac to 16-bit interleaved PCM at the file's own sample
 * rate and channel layout, so nothing is resampled at import and the cooked
 * bytes are the source material. AudioDevice resamples at play time if the
 * device runs at a different rate.
 *
 * Synchronous.
 *
 * The project-relative path is the clip's name: an absolute one would bake the
 * authoring machine's directory tree into every scene that names the sound.
 *
 * @param filePath Path to the sound file, absolute or project-relative.
 * @param resources Resource manager the decoded clip is added to.
 * @return Handle to the decoded clip, or an invalid handle if it did not decode.
 */
AudioClipHandle loadAudioClip(const std::string& filePath, ResourceManager& resources);

} // namespace Vkm::Engine
