#pragma once

#include <cstdint>

#include "core/engine_config.h"
#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

/// What the wire is: 'VK', so a stray datagram from something else is refused.
constexpr uint16_t NET_PROTOCOL_TAG = 0x564B;

/// Which version of it. Both ends run the same build, so a mismatch is refused.
constexpr uint8_t NET_PROTOCOL_VERSION = 9;

/**
 * @brief What a datagram is, read from the header every payload starts with.
 *
 * Closed: a game's data rides inside Snapshot or Command. Each carries a
 * 64-bit token; all but Hello and Challenge are dropped unread when the token
 * is not recognised (see NetServer::greet).
 */
enum class NetMessage : uint8_t {
    Hello     = 1,  ///< Client -> server: let me play; what I am, and my token.
    Welcome   = 2,  ///< Server -> client: you are player N, driving slot S.
    Refuse    = 3,  ///< Server -> client: no, and why.
    Goodbye   = 4,  ///< Either way: I am going, do not wait for me.
    Snapshot  = 5,  ///< Server -> client: the world, against what you last confirmed.
    Command   = 6,  ///< Client -> server: what I did, on the ticks I did it.
    Challenge = 7,  ///< Server -> client: say Hello again with this token, to show you hear me.
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

/// Who sent a payload, as far as its header can say.
enum class NetSender : uint8_t {
    ThisBuild,   ///< The same protocol version: the rest of the payload can be read.
    OtherBuild,  ///< This engine's wire at another version, saying Hello or Refuse.
    Unknown      ///< Anything else, which is dropped unread.
};

/**
 * @brief Start a payload: the tag, the version, what it is and the sender's token.
 *
 * @param out     The payload being written.
 * @param message What the payload is.
 * @param token   The token it carries.
 */
void writeMessageHeader(BitWriter& out, NetMessage message, uint64_t token);

/**
 * @brief Read and check the header every payload starts with.
 *
 * The header, the kinds Hello and Refuse, and a Refuse's reason keep this layout
 * in every version, so two builds can tell each other they differ. Everything
 * past them is read only at this build's version.
 *
 * @param in      The payload, read past the header on return.
 * @param message Receives the kind, unless the sender is Unknown.
 * @param token   Receives the token the payload carries.
 * @return Who sent it, as far as the header can say.
 */
NetSender readMessageHeader(BitReader& in, NetMessage& message, uint64_t& token);

/**
 * @brief What a process is while a session is open.
 *
 * Changes only when a session opens or closes, never inside one.
 */
enum class NetRole : uint8_t {
    Offline = 0,  ///< No session. Single player, and the editor before Play.
    Server,       ///< The authority. Decides everything, predicts nothing.
    Client,       ///< Predicts what it owns, is told the rest.

    /**
     * @brief A client whose server has gone or refused it, and which is not the authority.
     *
     * Decides nothing: made Offline, it would inherit a world it was only
     * shown, or play alone after a refused join. The world holds still; what
     * next is the game's, and `lastError()` says what happened.
     */
    Disconnected,
    Count
};

/// Whether this end decides what is true.
constexpr bool isAuthority(NetRole role) {
    return role == NetRole::Offline || role == NetRole::Server;
}

/**
 * @brief The largest scale factor the wire will carry, either sign.
 *
 * Scale is unquantised, so it is held to this at both ends, never one alone:
 * a Transform's by netScale, a NetSpawn's refused past it by isSayable. A
 * non-finite scale would fail every bounds test and leave the body invisible.
 */
constexpr float NET_MAX_SCALE = 1024.0f;

/**
 * @brief How far ahead of the newest snapshot a tick may claim to be.
 *
 * A track drops samples older than its newest, so one impossible tick would
 * freeze every body it reached. A minute at the fastest tick rate is past any
 * stall a connection survives.
 */
constexpr uint32_t NET_MAX_TICK_JUMP = Config::MAX_TICK_RATE * 60;

/**
 * @brief Player slots run from one; zero is "nobody", so a default id is not player one.
 */
using PlayerId = uint16_t;
constexpr PlayerId NO_PLAYER = 0;

} // namespace Vkm::Engine
