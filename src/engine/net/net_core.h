#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vkm_network/bit_stream.h"
#include "vkm_network/connection.h"
#include "vkm_network/udp_socket.h"

#include "net/wire/protocol.h"

namespace Vkm::Engine {

constexpr float NET_SNAPSHOT_RATE = 64.0f;  ///< The most the world goes out, in packets a second, whatever
                                            ///< the frame rate; see snapshotRate. Decides how late another
                                            ///< player looks: the interpolation delay is counted in
                                            ///< snapshots.

/**
 * @brief Snapshots a second a session of @p tickRate ticks a second sends and hears: NET_SNAPSHOT_RATE,
 *        or one a tick where ticks are fewer, since one sent between ticks says nothing new.
 *
 * @param tickRate The session's ticks a second; zero for none yet.
 * @return Snapshots a second.
 */
inline float snapshotRate(uint32_t tickRate) {
    return tickRate > 0 ? std::min(NET_SNAPSHOT_RATE, static_cast<float>(tickRate)) : NET_SNAPSHOT_RATE;
}

constexpr float NET_COMMAND_RATE = 64.0f;  ///< How often input goes up, in packets a second: how stale the
                                           ///< server's picture of a player's input may be. A tick the server
                                           ///< has no command for is a tick the player did not get.

constexpr uint32_t NET_MAX_TICK_RATE = static_cast<uint32_t>(2.0f * NET_COMMAND_RATE);
    ///< The fastest tick rate a networked session runs at: two commands to a command packet.
    ///< NetCommandBuffer::MAX_DEPTH, NET_COMMAND_REDUNDANCY and the pacing cushion are counted in commands
    ///< for it; faster, one packet fills the queue to the trim. host() and connect() refuse a faster rate.

/**
 * @brief What a session's server and client halves share: the socket, what the two
 *        ends agreed, and the buffers a datagram is built in.
 */
struct NetCore {
    static constexpr size_t MAX_READS_PER_FRAME = 512;  ///< Datagrams read in one frame. Draining until empty
                                                        ///< would make the frame's cost the sender's choice.
                                                        ///< What is left waits.

    Network::UdpSocket socket;
    uint32_t           tickRate = 0;  ///< Fixed for the session.

    uint32_t actionCount = 0;  ///< The project's action slots, fixed for the session.
    uint64_t actionNames = 0;  ///< Which action has which slot: this end's own, from
                               ///< InputMap::actionFingerprint; a server holds each Hello to it.

    uint64_t world = 0;  ///< Which world this end loaded, or zero for none.

    uint8_t     worldGeneration = 0;  ///< Which of the session's worlds this end holds: the server moves it
                                      ///< each time its world is replaced mid-session, never back to zero,
                                      ///< and a client takes the server's once it has loaded the same.
    std::string worldPath;            ///< The scene the world was loaded from, project-relative; empty if
                                      ///< none.

    uint32_t serverTick = 0;  ///< The authority's tick: its own count on a server; on a client, the newest a
                              ///< snapshot carried, since the two clocks are unrelated.

    std::string lastError;

    std::vector<uint8_t>  payload;   ///< What an outgoing message is written into.
    std::vector<uint8_t>  datagram;  ///< The datagram last received, or last framed to send.
    std::vector<uint16_t> acknowledged;
};

/**
 * @brief Send @p message, which carries nothing past its header, on @p connection.
 *
 * @param core       Its socket sends, and its buffers build the datagram.
 * @param connection Frames it, and names where it goes.
 * @param message    What it is, such as a Goodbye.
 * @param token      The token it carries.
 */
void sendBare(NetCore& core, Network::Connection& connection, NetMessage message, uint64_t token);

/**
 * @brief The payload of the datagram last received, past the connection's header.
 *
 * Empty for a datagram too short to carry that header, so its message header
 * reads as NetSender::Unknown.
 *
 * @param core Holds the datagram last received.
 * @return A reader over the payload.
 */
Network::BitReader receivedPayload(const NetCore& core);

} // namespace Vkm::Engine
