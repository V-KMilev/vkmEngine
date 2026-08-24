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
 * bytes with a decoder attached. That is one answer for a footstep and a music
 * bed alike, and it is the answer because a streamed clip would be the only
 * asset in the engine that keeps a file open past its load: a `Resource` is a
 * value a scene load builds in a staging ResourceManager and swaps in whole,
 * and a live decoder reading a file is not that. Playing then costs no decode,
 * which is what the footstep played fifty times a minute cares about.
 *
 * What it costs is visible rather than hidden: the cooked file IS these
 * samples, so a clip occupies as much memory as it does disk, and a project
 * that cannot afford a ten-minute music bed can see that before it ships one.
 * Streaming can arrive later as a second data source behind the same handle,
 * touching neither the component, the scene format nor this asset's identity -
 * which makes it a thing to add when a project needs it rather than a flag to
 * carry until one does.
 *
 * Samples are 16-bit signed because that is what the source material already is
 * (wav and flac are 16- or 24-bit, and mp3 decodes to it losslessly at this
 * depth), and because the mixer converts to float once per buffer regardless.
 * Float storage would double the footprint to save a conversion that happens
 * anyway.
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
