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
 * bytes are the source material. The mixer resamples at play time if the device
 * runs at a different rate, which is where that conversion belongs - a project
 * moved between a 44.1 kHz and a 48 kHz machine should not re-cook.
 *
 * Synchronous. The importer runs in the editor and the cooker, neither of which
 * has a frame to protect, and a decode measured in tens of milliseconds does
 * not earn the async lane that model and image import pay for.
 *
 * The project-relative path is the clip's name, matching how textures and
 * models are identified: an absolute one would bake the authoring machine's
 * directory tree into every scene that names the sound.
 *
 * @param filePath Path to the sound file, absolute or project-relative.
 * @param resources Resource manager the decoded clip is added to.
 * @return Handle to the decoded clip, or an invalid handle if it did not decode.
 */
AudioClipHandle loadAudioClip(const std::string& filePath, ResourceManager& resources);

} // namespace Vkm::Engine
