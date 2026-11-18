#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief What a process is while a session is open.
 *
 * Changes only when a session opens or closes, never inside one.
 */
enum class NetRole : uint8_t {
    Offline = 0,  ///< No session: single player, and the editor.
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

} // namespace Vkm::Engine
