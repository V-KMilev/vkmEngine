#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

/**
 * @brief A sound: fully decoded interleaved PCM, plus the rate and layout it was written at.
 *
 * Clips are decoded once, at load, and held as samples rather than as encoded
 * bytes with a decoder attached - one answer for a footstep and a music bed
 * alike. A streamed clip would be the only asset in the engine keeping a file
 * open past its load, and a `Resource` is a value a scene load builds in a
 * staging manager and swaps in whole; a live decoder is not that.
 *
 * The cost is visible rather than hidden: the cooked file IS these samples, so a
 * clip occupies as much memory as disk. Streaming can arrive later as a second
 * data source behind the same handle, touching neither the component nor the
 * scene format.
 *
 * Samples are 16-bit signed because that is what the source material already is,
 * and because the mixer converts to float once per buffer regardless.
 */
struct AudioClipAsset : public Resource {
    uint32_t sampleRate = 0;  ///< Frames per second, as authored; the mixer resamples if it differs.
    uint32_t channels   = 0;  ///< 1 = mono, 2 = stereo, up to MAX_AUDIO_CHANNELS; only a mono clip positions.

    /**
     * @brief Interleaved PCM, shared rather than owned outright.
     *
     * The one place in the engine where an asset's payload is refcounted, and
     * the reason is the mixer: a playing voice reads these samples from the
     * audio thread, while a scene load frees the asset that owns them from the
     * main thread without asking anyone first. Sharing ownership with the voice
     * is what turns that from a use-after-free into a sound that keeps playing
     * for the one frame it takes AudioSystem to notice the graph moved.
     *
     * Immutable by type, because two things now read it: nothing may edit a
     * clip's samples out from under a voice, and const is how that is said.
     */
    std::shared_ptr<const std::vector<int16_t>> samples;

    /// Total samples across all channels; 0 for a clip that carries none.
    size_t sampleCount() const { return samples ? samples->size() : 0; }

    /**
     * @brief Length in sample frames (one frame is one sample per channel).
     *
     * Derived rather than stored: the sample count and the channel count
     * already say it, and a third field could disagree with them.
     */
    uint64_t frameCount() const { return channels == 0 ? 0 : sampleCount() / channels; }

    /// Length in seconds, or 0 when the clip carries no rate.
    float duration() const {
        return sampleRate == 0 ? 0.0f : static_cast<float>(frameCount()) / static_cast<float>(sampleRate);
    }
};

using AudioClipHandle = Handle<AudioClipAsset>;

} // namespace Vkm::Engine
