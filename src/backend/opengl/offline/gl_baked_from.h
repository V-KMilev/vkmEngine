#pragma once

namespace Vkm::Engine {

/**
 * @brief The signature a cached bake was made from, and whether one was made.
 *
 * changed() both compares (the signature's `operator==`) and records, so a field cannot be
 * assigned in one place and left out of the comparison in another.
 *
 * @tparam Signature Everything the bake depends on, comparable for equality.
 */
template <typename Signature>
class BakedFrom {
    public:
        BakedFrom()  = default;
        ~BakedFrom() = default;

        BakedFrom(const BakedFrom& other) = default;
        BakedFrom& operator=(const BakedFrom& other) = default;

        BakedFrom(BakedFrom && other) = default;
        BakedFrom& operator=(BakedFrom && other) = default;

    public:
        /**
         * @brief Whether @p now differs from what is baked; adopts it if so.
         *
         * @param now What this frame would bake from.
         * @return Whether the bake has to run.
         */
        bool changed(const Signature& now) {
            if (m_baked && m_last == now) return false;
            m_last  = now;
            m_baked = true;
            return true;
        }

        /// Forget what was baked, so the next changed() answers true.
        void invalidate() { m_baked = false; }

        /// Whether a bake has run since construction or the last invalidate().
        bool baked() const { return m_baked; }

        /// What the last changed() that answered true was given.
        const Signature& last() const { return m_last; }

    private:
        Signature m_last{};
        bool      m_baked = false;
};

} // namespace Vkm::Engine
