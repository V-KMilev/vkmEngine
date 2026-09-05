#define VKM_LOG_CATEGORY "LOADER"

#include "loader/audio_loaders.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/project_paths.h"
#include "resource/resource_manager.h"

// miniaudio's implementation is provided once by the miniaudio module (linked
// into vkm_core); here we need only the decoder declarations.
#include "miniaudio.h"
#include "resource/asset_source_kind.h"

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
 * The hint is capped because it comes out of the file's own header and a
 * corrupt one can claim any length at all: a FLAC's STREAMINFO carries 36 bits
 * of frame count behind no audio frames whatsoever, and reserving what it asks
 * for turns a 44-byte file into an uncaught bad_alloc that takes the editor
 * with it. The cooked reader answers the same lie by refusing a count no bytes
 * back; an importer cannot know that yet, so it declines to trust the count at
 * all and lets the loop below grow the buffer for a clip that really is long.
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
        samples.insert(samples.end(), chunk.begin(),
                       chunk.begin() + static_cast<size_t>(framesRead * channels));

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
        LOG_ERROR("Failed to load sound from '%s' (not a wav/mp3/flac, or unreadable)",
                  resolved.c_str());
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
    clip.samples = std::make_shared<const std::vector<int16_t>>(std::move(samples));

    LOG_VERBOSE("Loaded sound '%s' (%.2fs, %u channel(s), %u Hz)", ref.c_str(),
                static_cast<double>(clip.duration()), clip.channels, clip.sampleRate);

    clip.sourceJson() = {{"kind", AssetSourceKind::FILE}, {"path", ref}};
    // The reference is the clip's name: the identity a scene resolves it by.
    return resources.add(std::move(clip), ref);
}

} // namespace Vkm::Engine
