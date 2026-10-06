#define VKM_LOG_CATEGORY "LOADER"

#include "import/audio_loaders.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "miniaudio.h"

#include "logger.h"

#include "io/project_paths.h"
#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

// Frames pulled per read. Big enough that a three-minute track is a few hundred
// reads, small enough that the last (partial) one wastes nothing worth naming.
constexpr ma_uint64 DECODE_CHUNK_FRAMES = 16384;

// The most the declared length is allowed to reserve: ten minutes at 48 kHz,
// which is past any music bed a project is likely to hold. A longer clip still
// loads whole - see decodeAll(), where this bounds an allocation hint and
// nothing else.
constexpr ma_uint64 MAX_RESERVE_FRAMES = 10 * 60 * 48000;

/**
 * @brief Pull every frame the decoder has into @p samples.
 *
 * Driven by short reads rather than by a declared length: an mp3's frame count
 * is an estimate until it has been walked, and a decoder is allowed to answer
 * MA_NOT_IMPLEMENTED for the question entirely. The length is still asked for
 * first, as a reserve hint, so the common case allocates once.
 *
 * The hint is capped at MAX_RESERVE_FRAMES because it comes out of the file's
 * own header, which a corrupt file can make claim any length.
 */
bool decodeAll(ma_decoder& decoder, uint32_t channels, std::vector<int16_t>& samples) {
    ma_uint64 declaredFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &declaredFrames) == MA_SUCCESS) {
        samples.reserve(static_cast<size_t>(std::min(declaredFrames, MAX_RESERVE_FRAMES) * channels));
    }

    std::vector<int16_t> chunk(static_cast<size_t>(DECODE_CHUNK_FRAMES * channels));
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result result =
            ma_decoder_read_pcm_frames(&decoder, chunk.data(), DECODE_CHUNK_FRAMES, &framesRead);
        samples.insert(
            samples.end(),
            chunk.begin(),
            chunk.begin() + static_cast<size_t>(framesRead * channels)
        );

        if (result == MA_AT_END) return true;
        if (result != MA_SUCCESS) return false;
        // A successful read of nothing would spin forever; treat it as the end,
        // which is what it is for every decoder that reports one.
        if (framesRead == 0) return true;
    }
}

} // namespace

AudioClipHandle loadAudioClip(const std::string& filePath, ResourceManager& resources) {
    // The reference is what the clip is named and recorded by; the resolved path
    // is only what the decoder opens.
    const std::string ref      = ProjectPaths::toProjectRelative(filePath);
    const std::string resolved = ProjectPaths::resolveProjectPath(ref).string();

    if (auto existing = resources.findByName<AudioClipAsset>(ref)) return existing;

    // Format is pinned to s16 - that is what the asset stores - while channels
    // and rate stay at 0, which asks the decoder for the file's own.
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);

    ma_decoder decoder;
    if (ma_decoder_init_file(resolved.c_str(), &config, &decoder) != MA_SUCCESS) {
        LOG_ERROR("Failed to load sound from '%s' (not a wav/mp3/flac, or unreadable)", resolved.c_str());
        return {};
    }

    AudioClipAsset clip;
    clip.channels   = decoder.outputChannels;
    clip.sampleRate = decoder.outputSampleRate;

    std::vector<int16_t> samples;
    const bool decoded = decodeAll(decoder, clip.channels, samples);
    ma_decoder_uninit(&decoder);

    if (!decoded || samples.empty()) {
        LOG_ERROR("Sound '%s' decoded to nothing", ref.c_str());
        return {};
    }
    clip.samples = ClipSamples(std::move(samples));

    LOG_VERBOSE(
        "Loaded sound '%s' (%.2fs, %u channel(s), %u Hz)",
        ref.c_str(),
        static_cast<double>(clip.duration()),
        clip.channels,
        clip.sampleRate
    );

    clip.sourceJson() = {{"kind", AssetSourceKind::FILE}, {AssetSourceKey::PATH, ref}};
    return resources.add(std::move(clip), ref);
}

} // namespace Vkm::Engine
