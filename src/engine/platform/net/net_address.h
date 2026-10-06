#pragma once

#include <cstdint>
#include <string>

namespace Vkm::Engine {

/**
 * @brief Where a datagram came from, or is going to: an IPv4 host and a port.
 *
 * Host byte order; converted at the socket.
 */
struct NetAddress {
    uint32_t ipv4 = 0;   ///< Host byte order: 0x7F000001 is 127.0.0.1.
    uint16_t port = 0;

    /// True when this names somewhere - a zero address is the null one.
    explicit operator bool() const { return ipv4 != 0 && port != 0; }

    bool operator==(const NetAddress& other) const {
        return ipv4 == other.ipv4 && port == other.port;
    }
    bool operator!=(const NetAddress& other) const { return !(*this == other); }

    /**
     * @brief Read "host:port", or "host" with @p fallbackPort.
     *
     * A caller passes the project's port as the fallback. A name that is not
     * dotted quad goes through the system resolver.
     *
     * @param text         Host, or host:port.
     * @param fallbackPort Used when @p text carries no port.
     * @return The address, or a null one when @p text names nowhere.
     */
    static NetAddress parse(const std::string& text, uint16_t fallbackPort = 0);

    /**
     * @brief Read a port: decimal digits and nothing else, 1 to 65535.
     *
     * Refused whole: "27015x" is not 27015, which they did not ask for.
     *
     * @param text The digits.
     * @param port Set only when @p text is a port.
     * @return False when @p text is not one.
     */
    static bool parsePort(const std::string& text, uint16_t& port);

    /// "127.0.0.1:27015", for a log line or a readout.
    std::string toString() const;
};

} // namespace Vkm::Engine
