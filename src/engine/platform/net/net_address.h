#pragma once

#include <cstdint>
#include <string>

namespace Vkm::Engine {

/**
 * @brief Where a datagram came from, or is going to: an IPv4 host and a port.
 *
 * A value, compared and copied freely. Held in host byte order and converted at
 * the socket, so nothing above this layer has to know that a wire is
 * big-endian - the one place that does is the one place that talks to the
 * operating system.
 *
 * IPv4 only, deliberately. A second family is a second parse, a second socket
 * family and a second address size on every path that carries one, and no game
 * this engine runs needs it today.
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
     * The spelling a player types. A bare host is accepted because the port is
     * the game's rather than the player's - they were told a machine, not a
     * number - so a caller passes the project's own port as the fallback.
     *
     * A name that is not already dotted quad is resolved through the system
     * resolver, so "localhost" and a hostname both work.
     *
     * @param text         Host, or host:port.
     * @param fallbackPort Used when @p text carries no port.
     * @return The address, or a null one when @p text names nowhere.
     */
    static NetAddress parse(const std::string& text, uint16_t fallbackPort = 0);

    /// "127.0.0.1:27015", for a log line or a readout.
    std::string toString() const;
};

} // namespace Vkm::Engine
