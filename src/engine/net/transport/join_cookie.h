#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/net/net_address.h"

namespace Vkm::Engine {

/**
 * @brief SipHash-2-4 of @p size bytes, under the 128-bit key (@p k0, @p k1).
 *
 * A MAC: without the key, one output says nothing of another. FNV-1a can be
 * steered by the sender, so it never stands in for this.
 *
 * @param k0   First half of the key, as SipHash reads its bytes 0 to 7.
 * @param k1   Second half, bytes 8 to 15.
 * @param data Bytes to hash.
 * @param size Byte count.
 * @return The 64-bit tag.
 */
uint64_t sipHash24(uint64_t k0, uint64_t k1, const uint8_t* data, size_t size);

/**
 * @brief The token a server gives an address that asks to join, and takes back
 *        as proof that the address hears what is sent to it.
 *
 * Stateless - a keyed hash of the address and the time window - so a forged
 * Hello costs a hash and a reply no larger, and crowds out no real join.
 * Accepted for its window and the next. The key comes from the operating
 * system, so tokens cannot be predicted and a restart voids them.
 */
class NetJoinCookie {
    public:
        /// How long one window lasts, in seconds; a token is good for one to two of them.
        static constexpr double WINDOW_SECONDS = 10.0;

    public:
        NetJoinCookie() = default;
        ~NetJoinCookie() = default;

        NetJoinCookie(const NetJoinCookie& other) = default;
        NetJoinCookie& operator=(const NetJoinCookie& other) = default;

        NetJoinCookie(NetJoinCookie && other) = default;
        NetJoinCookie& operator=(NetJoinCookie && other) = default;

    public:
        /// Draw a new key from the operating system. Every token given before is void.
        void rekey();

        /**
         * @brief The token for @p to, as of @p now.
         *
         * @param to  The address that asked.
         * @param now Seconds on a clock that only moves forward.
         * @return The token.
         */
        uint64_t issue(const NetAddress& to, double now) const;

        /**
         * @brief Whether @p token is one issue() gave @p from, this window or the last.
         *
         * @param from  The address the token came back from.
         * @param token What it carried.
         * @param now   Seconds, on the clock issue() was given.
         * @return True when the token proves the address heard the Challenge.
         */
        bool accepts(const NetAddress& from, uint64_t token, double now) const;

    private:
        uint64_t forWindow(const NetAddress& address, uint64_t window) const;

    private:
        uint64_t m_k0 = 0;
        uint64_t m_k1 = 0;
};

} // namespace Vkm::Engine
