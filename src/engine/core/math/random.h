#pragma once

#include <cstdint>
#include <cmath>
#include <atomic>
#include <chrono>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief PCG32 pseudo-random generator - small, fast, and statistically strong.
 *
 * The same PRNG family PBRT uses: 16 bytes of state, no global tables, and a
 * 64-bit period selectable per stream so independent sequences never correlate.
 * Deterministic and seedable, so a render reproduces exactly and each thread /
 * pixel / sample can own its own stream.
 *
 * Reference: M.E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient
 * Statistically Good Algorithms for Random Number Generation" (2014).
 */
class Rng {
    public:
        Rng() = default;
        ~Rng() = default;

        Rng(const Rng& other) = default;
        Rng& operator=(const Rng& other) = default;

        Rng(Rng && other) noexcept = default;
        Rng& operator=(Rng && other) noexcept = default;

        /**
         * @brief Seed with an explicit state and (optionally) stream selector.
         * @param seed   Initial state; any 64-bit value.
         * @param stream Sequence selector - two RNGs with the same seed but
         *               different streams produce uncorrelated sequences.
         */
        explicit Rng(uint64_t seed, uint64_t stream = DEFAULT_STREAM) {
            this->seed(seed, stream);
        }

    public:
        /**
         * @brief Re-seed in place, discarding the current sequence position.
         * @param seed   Initial state; any 64-bit value.
         * @param stream Sequence selector; see the constructor.
         */
        void seed(uint64_t seed, uint64_t stream = DEFAULT_STREAM) {
            m_state = 0u;
            m_inc   = (stream << 1u) | 1u;   // inc must be odd
            nextU32();
            m_state += seed;
            nextU32();
        }

        /**
         * @brief Advance the state and return the next raw 32-bit value.
         *
         * The primitive every other draw is built on: one 64-bit LCG step
         * followed by PCG's xorshift-then-rotate output permutation.
         */
        uint32_t nextU32() {
            const uint64_t old = m_state;
            m_state = old * 6364136223846793005ULL + m_inc;
            const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
            const uint32_t rot        = static_cast<uint32_t>(old >> 59u);
            return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
        }

        float nextFloat()                     { return (nextU32() >> 8) * (1.0f / 16777216.0f); }
        float nextFloat(float min, float max) { return min + (max - min) * nextFloat(); }
        int nextInt(int min, int max)         { return min + static_cast<int>(boundedU32(static_cast<uint32_t>(max - min) + 1u)); }
        bool nextBool()                       { return (nextU32() >> 31u) != 0u; }

    private:
        /**
         * @brief Uniform 32-bit value in [0, range), unbiased.
         *
         * Lemire, "Fast Random Integer Generation in an Interval" (2019): a
         * multiply-and-shift that only rejects in the small leftover interval.
         */
        uint32_t boundedU32(uint32_t range) {
            uint32_t x = nextU32();
            uint64_t m = static_cast<uint64_t>(x) * static_cast<uint64_t>(range);
            uint32_t low = static_cast<uint32_t>(m);
            if (low < range) {
                const uint32_t threshold = (0u - range) % range;
                while (low < threshold) {
                    x   = nextU32();
                    m   = static_cast<uint64_t>(x) * static_cast<uint64_t>(range);
                    low = static_cast<uint32_t>(m);
                }
            }
            return static_cast<uint32_t>(m >> 32u);
        }

    private:
        static constexpr uint64_t DEFAULT_STREAM = 0xda3e39cb94b95bdbULL;  ///< Canonical PCG stream, used as the default selector.

    private:
        uint64_t m_state = 0x853c49e6748fea9bULL;  ///< LCG state; advanced on every draw.
        uint64_t m_inc   = DEFAULT_STREAM;          ///< Stream increment (kept odd); fixes which sequence this draws.
};

/**
 * @brief Ray-tracing random toolkit: scalar shortcuts plus the standard
 *        direction/point samplers, layered over the Rng primitive.
 *
 * Every sampler takes an explicit Rng& (the deterministic path - seed it per
 * thread / pixel for reproducible renders). Each also has a no-argument
 * overload that draws from a per-thread default generator (rng()), for casual
 * game-side use where reproducibility doesn't matter.
 */
namespace Random {

/**
 * @brief Per-thread default generator.
 *
 * Lazily seeded once per thread from a clock sample mixed with a global
 * counter, so threads get distinct, uncorrelated streams. Non-reproducible by
 * design; construct your own Rng(seed) when you need determinism.
 */
inline Rng& rng() {
    thread_local Rng t_generator = [] {
        static std::atomic<uint64_t> s_counter{0};
        const uint64_t n = s_counter.fetch_add(1, std::memory_order_relaxed);
        const uint64_t t = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return Rng(t ^ (n * 0x9E3779B97F4A7C15ULL), n + 1u);
    }();
    return t_generator;
}

/**
 * @brief Uniform float in [0, 1), from the per-thread generator.
 */
inline float value() { return rng().nextFloat(); }

/**
 * @brief Uniform float in [min, max), from the per-thread generator.
 */
inline float range(float min, float max) { return rng().nextFloat(min, max); }

/**
 * @brief Uniform integer in [min, max] (inclusive), from the per-thread generator.
 */
inline int range(int min, int max) { return rng().nextInt(min, max); }

/**
 * @brief Fair coin flip, from the per-thread generator.
 */
inline bool boolean() { return rng().nextBool(); }

} // namespace Random

} // namespace Vkm::Engine::Math
