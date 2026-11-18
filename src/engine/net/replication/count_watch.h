#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief Watches a replicated count and says when it rises, never for the value it first sees.
 *
 * A client starts before its first snapshot, so whatever the server counted before this end
 * looked would otherwise arrive as news: how an effect a server counts plays at every end. A fall
 * is a reset, taken silently. Also fits a set of bits that only gains them.
 */
class CountWatch {
    public:
        CountWatch() = default;
        ~CountWatch() = default;

        CountWatch(const CountWatch& other) = default;
        CountWatch& operator=(const CountWatch& other) = default;

        CountWatch(CountWatch && other) = default;
        CountWatch& operator=(CountWatch && other) = default;

    public:
        /**
         * @brief Whether @p now is above the count last seen; false for the first one seen.
         *
         * @param now The count as this end was last told.
         * @return True once for each rise after the first value.
         */
        bool rose(uint32_t now) {
            const bool rising = m_watched && now > m_seen;
            m_seen    = now;
            m_watched = true;
            return rising;
        }

    private:
        uint32_t m_seen    = 0;
        bool     m_watched = false;  ///< Whether a value has been seen yet.
};

} // namespace Vkm::Engine
