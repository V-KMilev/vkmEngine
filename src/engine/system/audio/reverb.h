#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief A room's reverberation: Schroeder's design as Freeverb tunes it - damped combs in
 *        parallel, then all-passes in series, each output channel's lines a little longer than
 *        the last so the tail is wide.
 *
 * Plain C++, so it is tested without a mixer; AudioDevice runs it as a node. The decay, level and
 * silence are set from the main thread and read by the mixer's, so they are atomics; everything
 * else is the mixer thread's after prepare().
 */
class ReverbTank {
    public:
        ReverbTank() = default;
        ~ReverbTank() = default;

        ReverbTank(const ReverbTank& other) = delete;
        ReverbTank& operator=(const ReverbTank& other) = delete;

        ReverbTank(ReverbTank && other) = delete;
        ReverbTank& operator=(ReverbTank && other) = delete;

    public:
        /**
         * @brief Size the lines for @p sampleRate and @p channels; clears any tail.
         *
         * Allocates, so it runs before the mixer thread reads the tank.
         *
         * @param sampleRate Rate the tank runs at.
         * @param channels   Interleaved channels in and out.
         */
        void prepare(uint32_t sampleRate, uint32_t channels);

        /**
         * @brief How long the tail takes to fall by 60 dB.
         *
         * @param seconds Held within the tank's range; non-finite reads as the shortest.
         */
        void setDecay(float seconds);

        /**
         * @brief The tail's level beside the dry sound.
         *
         * @param wet Linear; non-positive or non-finite silences it.
         */
        void setWet(float wet);

        /// Let the tail go: the next block fades it out and the lines are empty after it.
        void silence() { m_silencing.store(true, std::memory_order_relaxed); }

        /**
         * @brief Reverberate @p frames of @p in into @p out: the tail alone, scaled by the level.
         *
         * Every channel of the input feeds every channel's tank, so a sound heard in one ear rings
         * in both.
         *
         * @param in     Interleaved input, the channel count prepare() was given; null is silence.
         * @param out    Interleaved output; may not alias @p in.
         * @param frames Frames to process.
         */
        void process(const float* in, float* out, uint32_t frames);

    private:
        struct Line {
            std::vector<float> buffer;
            uint32_t           at       = 0;
            float              feedback = 0.0f;
            float              damped   = 0.0f;  ///< A comb's one-pole low-pass state.
        };

    private:
        /// Recompute the combs' feedback for the decay last set, if it moved.
        void retune();

    private:
        uint32_t           m_rate     = 48000;
        uint32_t           m_channels = 0;
        std::vector<Line>  m_combs;      ///< Each channel's combs in turn.
        std::vector<Line>  m_allpasses;  ///< Each channel's all-passes in turn.
        std::atomic<float> m_decay{1.0f};
        std::atomic<float> m_wet{0.0f};
        std::atomic<bool>  m_silencing{false};
        float              m_tunedDecay = -1.0f;
};

} // namespace Vkm::Engine
