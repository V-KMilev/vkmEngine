#define VKM_LOG_CATEGORY "LOADER"

#include "import/audio_import.h"

#include <utility>

#include <nlohmann/json.hpp>

#include "vkm_audio/decode.h"
#include "vkm_log/logger.h"

#include "io/project/roots.h"
#include "io/asset/recipe/asset_source.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

AudioClipHandle loadAudioClip(const std::string& filePath, ResourceManager& resources) {
    // The reference is what the clip is named and recorded by; the resolved path
    // is only what the decoder opens.
    const std::string ref      = Roots::toProjectRelative(filePath);
    const std::string resolved = Roots::resolveProjectPath(ref).string();

    if (auto existing = resources.findByName<AudioClipAsset>(ref)) return existing;

    AudioClipAsset clip;
    const Audio::Decoded decoded = Audio::decodeFile(resolved, clip);
    if (decoded == Audio::Decoded::Unreadable) {
        LOG_ERROR("Failed to load sound from '%s' (not a wav/mp3/flac, or unreadable)", resolved.c_str());
        return {};
    }
    if (decoded == Audio::Decoded::Empty) {
        LOG_ERROR("Sound '%s' decoded to nothing", ref.c_str());
        return {};
    }

    LOG_VERBOSE(
        "Loaded sound '%s' (%.2fs, %u channel(s), %u Hz)",
        ref.c_str(),
        static_cast<double>(clip.duration()),
        clip.channels,
        clip.sampleRate
    );

    clip.sourceJson() = {{AssetSourceKey::KIND, AssetSourceKind::FILE}, {AssetSourceKey::PATH, ref}};
    return resources.add(std::move(clip), ref);
}

} // namespace Vkm::Engine
