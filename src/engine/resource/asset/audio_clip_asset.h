#pragma once

#include "vkm_audio/clip.h"

#include "resource/resource.h"
#include "resource/handle.h"

namespace Vkm::Engine {

/**
 * @brief A sound: vkmAudio's decoded clip, as an asset.
 *
 * Decoded once at load, not streamed: a `Resource` is a value built in staging
 * and swapped in whole, which a live decoder is not. The cooked file IS these
 * samples, so a clip takes as much memory as disk.
 */
struct AudioClipAsset : public Resource, public Audio::Clip {};

using AudioClipHandle = Handle<AudioClipAsset>;

} // namespace Vkm::Engine
