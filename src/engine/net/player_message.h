#pragma once

#include <cstdint>
#include <vector>

#include "ecs/entity.h"
#include "net/wire/protocol.h"

namespace Vkm::Engine {

/**
 * @brief What a player sent with NetSession::sendMessage, on the server.
 *
 * Each message is delivered once, in the order its player sent it, at the event flush after it
 * arrived - not with any tick's command, since a message is not input for a moment. One from a
 * player who left before that flush goes with them, as their seat has.
 */
struct PlayerMessageEvent {
    PlayerId             player = NO_PLAYER;
    EntityId             entity;  ///< What that player drives.
    std::vector<uint8_t> bytes;
};

} // namespace Vkm::Engine
