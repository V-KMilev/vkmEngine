#pragma once

#include <cstdint>

#include "core/engine_config.h"

namespace Vkm::Engine {

/// What the wire is: 'VK', so a stray datagram from something else is refused.
constexpr uint16_t NET_PROTOCOL_TAG = 0x564B;

/// Which version of it. Both ends run the same build, so a mismatch is refused.
constexpr uint8_t NET_PROTOCOL_VERSION = 1;

/**
 * @brief What a datagram is, read from its first field.
 *
 * Small and closed. Everything a game sends rides inside Snapshot or Command;
 * a game never adds a kind here, because a connection that could be told to
 * expect an unknown message is a connection whose reader has to guess.
 */
enum class NetMessage : uint8_t {
    Hello    = 1,  ///< Client -> server: I would like to play, and here is what I am.
    Welcome  = 2,  ///< Server -> client: you are player N, the world is at tick T.
    Refuse   = 3,  ///< Server -> client: no, and why.
    Goodbye  = 4,  ///< Either way: I am going, do not wait for me.
    Snapshot = 5,  ///< Server -> client: the world, against what you last confirmed.
    Command  = 6,  ///< Client -> server: what I did, on the ticks I did it.
    Count
};

/**
 * @brief What a reliable message is.
 *
 * Small and closed, like NetMessage. These are the things that happen once and
 * change what the world *is* rather than where it is, so no later packet would
 * repeat them and they have to be delivered rather than described.
 */
enum class NetEvent : uint8_t {
    Spawn   = 1,  ///< This prefab now exists, at this slot, here.
    Despawn = 2,  ///< That entity is gone; take its subtree with it.
    Count
};

/**
 * @brief Why a server would not take a client.
 *
 * Sent so the refusal can be shown rather than guessed at from a closed socket.
 */
enum class NetRefusal : uint8_t {
    Full      = 1,  ///< Every seat is taken.
    Mismatch  = 2,  ///< A different build, or a different world.
    Declined  = 3,  ///< The game itself said no.
    Count
};

/// A human-readable reason, for the log line and the client's own message.
const char* toString(NetRefusal reason);

/**
 * @brief What a process is while a session is open.
 *
 * Read by gameplay to answer "is this mine to decide". It changes only when a
 * session opens or closes, never inside one - which is weaker than "settled at
 * startup", and the weaker claim is the true one: a game joins from a menu, and
 * a menu runs long after startup.
 */
enum class NetRole : uint8_t {
    Offline = 0,  ///< No session. Single player, and the editor before Play.
    Server,       ///< The authority. Decides everything, predicts nothing.
    Client,       ///< Predicts what it owns, is told the rest.

    /**
     * @brief A client whose server has gone, and which is not the authority.
     *
     * A separate state from Offline rather than the same one, because the two
     * are opposites about the one question that matters. Offline decides
     * everything because there is nobody else; this end decides nothing because
     * it spent the whole session being told and has just lost the teller. Made
     * Offline instead, a dropped client would inherit a world it had only ever
     * been shown - every body it never owned handed to its physics at once from
     * interpolated positions, and every character suddenly its own.
     *
     * The world holds still here. What to do about it is the game's - a
     * message, a menu, a lobby - and `lastError()` says what happened.
     */
    Disconnected,
    Count
};

/// Whether this end decides what is true.
constexpr bool isAuthority(NetRole role) {
    return role == NetRole::Offline || role == NetRole::Server;
}

/**
 * @brief The highest scene slot the wire will name.
 *
 * A slot arrives from the network and reaches SlotAllocator::allocateAt, which
 * grows its table to whatever index it is handed - so an unbounded one is a
 * datagram that asks this process for sixteen gigabytes. No scene the engine
 * runs comes near this, and a message naming a slot past it is refused whole.
 */
constexpr uint32_t NET_MAX_SLOT = 1u << 24;

/**
 * @brief The largest scale factor the wire will carry, either sign.
 *
 * Position and rotation are quantised and so are bounded by construction;
 * scale is the one field carried as a raw float. It reaches a Transform and
 * from there every matrix the entity is in, so a value that is not finite
 * turns every bounds test against it false and leaves the body permanently
 * invisible rather than merely the wrong size.
 */
constexpr float NET_MAX_SCALE = 1024.0f;

/**
 * @brief How far ahead of the newest snapshot a tick may claim to be.
 *
 * The clock that decides what is drawn runs in the server's tick numbering, and
 * it only ever moves forward - so one impossible value puts it somewhere no
 * sample will ever reach and every body freezes for the rest of the session. A
 * minute at the fastest tick rate the engine allows is far past any stall a
 * connection survives, and nothing legitimate reaches it.
 */
constexpr uint32_t NET_MAX_TICK_JUMP = Config::MAX_TICK_RATE * 60;

/**
 * @brief Player slots run from one; zero is "nobody", so a default-constructed id is
 * not accidentally player one.
 */
using PlayerId = uint16_t;
constexpr PlayerId NO_PLAYER = 0;

} // namespace Vkm::Engine
