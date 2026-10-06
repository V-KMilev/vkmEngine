#pragma once

#include <cstdint>

namespace Vkm::Engine::Math {

/**
 * @brief PCG32 pseudo-random generator (O'Neill, 2014).
 *
 * Deterministic and seedable, so each owner can have its own reproducible stream.
 */
class Rng {
    public:
        Rng() = default;

        /**
         * @brief Seed with an explicit state and (optionally) stream selector.
         * @param seed   Initial state; any value.
         * @param stream Sequence selector; same seed, different stream is uncorrelated.
         */
        explicit Rng(uint64_t seed, uint64_t stream = DEFAULT_STREAM) {
            this->seed(seed, stream);
        }

        ~Rng() = default;

        Rng(const Rng& other) = default;
        Rng& operator=(const Rng& other) = default;

        Rng(Rng && other) noexcept = default;
        Rng& operator=(Rng && other) noexcept = default;

    public:
        /**
         * @brief Re-seed in place, discarding the current sequence position.
         * @param seed   Initial state; any value.
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
         * One LCG step, then PCG's xorshift-then-rotate output permutation.
         *
         * @return A uniform value over the full 32-bit range.
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
        int nextInt(int min, int max) {
            return min + static_cast<int>(boundedU32(static_cast<uint32_t>(max - min) + 1u));
        }
        bool nextBool()                       { return (nextU32() >> 31u) != 0u; }

    private:
        /// Canonical PCG stream.
        static constexpr uint64_t DEFAULT_STREAM = 0xda3e39cb94b95bdbULL;

    private:
        /**
         * @brief Uniform 32-bit value in [0, range), unbiased.
         *
         * Lemire (2019): multiply-and-shift, rejecting only in the leftover interval.
         *
         * @param range One past the largest value wanted; non-zero.
         * @return A value in [0, range).
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
        uint64_t m_state = 0x853c49e6748fea9bULL;  ///< LCG state.
        uint64_t m_inc   = DEFAULT_STREAM;         ///< Stream increment, kept odd.
};

/**
 * @brief The casual draws: a float, a range, a coin, off a per-thread generator.
 *
 * For draws that need not reproduce. Anything that must owns and seeds its own
 * Rng: a shared per-thread stream cannot be replayed and interleaves callers.
 */
namespace Random {

/**
 * @brief Per-thread default generator.
 *
 * Seeded lazily per thread from a clock sample and a global counter, so streams
 * are distinct. Not inline: no engine header carries a thread_local
 * (docs/guides/implementation.md, Threads).
 *
 * @return This thread's generator.
 */
Rng& rng();

/**
 * @brief Uniform float in [0, 1), from the per-thread generator.
 *
 * @return The draw.
 */
inline float value() { return rng().nextFloat(); }

/**
 * @brief Uniform float in [min, max), from the per-thread generator.
 *
 * @param min Lowest value drawn.
 * @param max Upper bound, never drawn.
 * @return The draw.
 */
inline float range(float min, float max) { return rng().nextFloat(min, max); }

/**
 * @brief Uniform integer in [min, max], from the per-thread generator.
 *
 * @param min Lowest value drawn.
 * @param max Highest value drawn; not below @p min.
 * @return The draw.
 */
inline int range(int min, int max) { return rng().nextInt(min, max); }

/**
 * @brief Fair coin flip, from the per-thread generator.
 *
 * @return The draw.
 */
inline bool boolean() { return rng().nextBool(); }

} // namespace Random

} // namespace Vkm::Engine::Math
