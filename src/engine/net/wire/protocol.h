#pragma once

#include <cstdint>

#include "vkm_network/bit_stream.h"

namespace Vkm::Engine {

constexpr uint16_t NET_PROTOCOL_TAG = 0x564B;  ///< What the wire is: 'VK', so a stray datagram from something
                                               ///< else is refused.

constexpr uint8_t NET_PROTOCOL_VERSION = 15;  ///< Which version of it. Both ends run the same build, so a
                                              ///< mismatch is refused.

/**
 * @brief What a datagram is, read from the header every payload starts with.
 *
 * Closed: a game's data rides inside Snapshot or Command. Each carries a
 * 64-bit token; all but Hello and Challenge are dropped unread when the token
 * is not recognized (see NetServer::receive).
 */
enum class NetMessage : uint8_t {
    Hello     = 1,  ///< Client -> server: let me play; what I am, and my token.
    Welcome   = 2,  ///< Server -> client: you are player N, driving slot S.
    Refuse    = 3,  ///< Server -> client: no, and why.
    Goodbye   = 4,  ///< Either way: I am going, do not wait for me.
    Snapshot  = 5,  ///< Server -> client: the world, against what you last confirmed.
    Command   = 6,  ///< Client -> server: what I did, on the ticks I did it.
    Challenge = 7,  ///< Server -> client: say Hello again with this token, to show you hear me; then your
                    ///< Hello's, to show I heard you.
    World     = 8,  ///< Server -> client: the world is now this scene; load it, and drive slot S.
    Count
};

/**
 * @brief Why a server would not take a client.
 *
 * Sent so the refusal can be shown, not guessed at.
 */
enum class NetRefusal : uint8_t {
    Full      = 1,  ///< Every seat is taken.
    Mismatch  = 2,  ///< A different build, or a different world.
    Declined  = 3,  ///< The game itself said no.
    TickRate  = 4,  ///< The two ends tick at different rates.
    Count
};

/// A human-readable reason, for the log line and the client's own message.
const char* toString(NetRefusal reason);

/**
 * @brief Who sent a payload, as far as its header can say.
 */
enum class NetSender : uint8_t {
    ThisBuild,   ///< The same protocol version: the rest of the payload can be read.
    OtherBuild,  ///< This engine's wire at another version, saying Hello, Challenge or Refuse.
    Unknown      ///< Anything else, which is dropped unread.
};

constexpr uint32_t NET_MESSAGE_HEADER_BITS = 16 + 8 + 8 + 64;  ///< What writeMessageHeader writes: the tag,
                                                               ///< the version, what it is, the token.

/**
 * @brief Start a payload: the tag, the version, what it is and the sender's token.
 *
 * @param out     The payload being written.
 * @param message What the payload is.
 * @param token   The token it carries.
 * @param version The version of the wire it is written at: this build's, but for a Challenge to
 *                another version's Hello (NetServer::challenge).
 */
void writeMessageHeader(
    Network::BitWriter& out,
    NetMessage message,
    uint64_t token,
    uint8_t version = NET_PROTOCOL_VERSION
);

/**
 * @brief Read and check the header every payload starts with.
 *
 * The header and the kinds Hello, Challenge and Refuse keep this layout in every
 * version, so two builds can tell each other they differ once the joining one has
 * echoed its token; a Challenge's echo and a Refuse's reason follow the header,
 * read by the client. Everything else past the header is read only at this
 * build's version.
 *
 * @param in      The payload, read past the header on return.
 * @param message Receives the kind, unless the sender is Unknown.
 * @param token   Receives the token the payload carries.
 * @param version Receives, when given, the version it is written at, unless the sender is Unknown.
 * @return Who sent it, as far as the header can say.
 */
NetSender readMessageHeader(
    Network::BitReader& in,
    NetMessage& message,
    uint64_t& token,
    uint8_t* version = nullptr
);

constexpr float NET_MAX_SCALE = 1024.0f;  ///< The largest scale factor the wire will carry, either sign.
                                          ///< Scale is unquantized, so it is held to this at both ends, never
                                          ///< one alone: a Transform's by netScale, a NetSpawn's refused past
                                          ///< it by isSayable. A non-finite scale would fail every bounds
                                          ///< test and leave the body invisible.


} // namespace Vkm::Engine
