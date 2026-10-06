#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

/**
 * @brief A clip's interleaved PCM, shared with every voice playing it.
 *
 * Refcounted so a voice on the audio thread outlives a scene load freeing the
 * asset (see AudioDevice). The filling constructor lives in vkm_core: the
 * control block carries the code that frees it, and a gameplay module's code
 * may be unmapped by a hot reload before the last reference drops.
 *
 * Immutable: nothing may edit a clip's samples out from under a voice.
 */
class ClipSamples {
    public:
        ClipSamples() = default;

        /**
         * @brief Take @p samples as a clip's buffer, shared from vkm_core.
         *
         * @param samples Interleaved PCM, moved in.
         */
        explicit ClipSamples(std::vector<int16_t> samples);

        ~ClipSamples() = default;

        ClipSamples(const ClipSamples& other) = default;
        ClipSamples& operator=(const ClipSamples& other) = default;

        ClipSamples(ClipSamples && other) = default;
        ClipSamples& operator=(ClipSamples && other) = default;

    public:
        explicit operator bool() const noexcept { return m_samples != nullptr; }

        const std::vector<int16_t>& operator*() const { return *m_samples; }
        const std::vector<int16_t>* operator->() const { return m_samples.get(); }

    private:
        std::shared_ptr<const std::vector<int16_t>> m_samples;
};

/**
 * @brief A sound: fully decoded interleaved PCM, plus the rate and layout it was written at.
 *
 * Decoded once at load, not streamed: a `Resource` is a value built in staging
 * and swapped in whole, which a live decoder is not. The cooked file IS these
 * samples, so a clip takes as much memory as disk.
 */
struct AudioClipAsset : public Resource {
    uint32_t sampleRate = 0;  ///< Frames per second, as authored; AudioDevice resamples if it differs.
    uint32_t channels   = 0;  ///< 1 = mono, 2 = stereo, up to MAX_AUDIO_CHANNELS; only a mono clip positions.

    ClipSamples samples;  ///< Interleaved PCM, shared with the voices playing it.

    /// Total samples across all channels.
    size_t sampleCount() const { return samples ? samples->size() : 0; }

    /**
     * @brief Length in sample frames (one frame is one sample per channel).
     *
     * @return The frame count; 0 for a clip with no channels.
     */
    uint64_t frameCount() const { return channels == 0 ? 0 : sampleCount() / channels; }

    /// Length in seconds, or 0 when the clip carries no rate.
    float duration() const {
        return sampleRate == 0 ? 0.0f : static_cast<float>(frameCount()) / static_cast<float>(sampleRate);
    }
};

using AudioClipHandle = Handle<AudioClipAsset>;

} // namespace Vkm::Engine
