#pragma once

#include <array>
#include <cstdint>

#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

inline constexpr uint32_t MAX_INPUT_ACTIONS = 32;  ///< Most actions one command can carry: fixed, so a
                                                   ///< command copies and goes on a wire with no heap
                                                   ///< allocation per tick. An action past the cap has no
                                                   ///< slot, which InputMap logs.

inline constexpr uint32_t MAX_COMMAND_PAYLOAD = 16;  ///< Bytes of a game's own a command can carry
                                                     ///< (InputCommand::payload).

/**
 * @brief Everything one simulation tick was told to do, as a value.
 *
 * Built per tick from the axes as they stand and the edges latched since the previous one, so a
 * fixed update neither drops a tap between ticks nor repeats a press across a slow frame's ticks.
 * sequence and tick differ because a replay re-runs old ticks under their original sequence.
 */
struct InputCommand {
    uint32_t sequence = 0;  ///< Monotonic command number; what an ack names.
    uint32_t tick     = 0;  ///< Simulation tick this command drives.

    std::array<float, MAX_INPUT_ACTIONS> axis{};  ///< Per-action value, -1..1, indexed by InputMap::indexOf.

    uint32_t pressed  = 0;  ///< Bit per action: became active since the last command.
    uint32_t released = 0;  ///< Bit per action: stopped being active since the last command.

    static_assert(
        MAX_INPUT_ACTIONS <= 32,
        "pressed / released carry one bit per action in a uint32_t, "
        "so an action past bit 31 has nowhere to be recorded"
    );

    glm::quat view{1.0f, 0.0f, 0.0f, 0.0f};  ///< Where the player was looking when this input was taken, so a
                                             ///< tick's movement depends on its command alone, not on a
                                             ///< camera that has since moved. A quaternion, so there is no
                                             ///< Euler convention; identity (looking down -Z) until set.

    std::array<uint8_t, MAX_COMMAND_PAYLOAD> payload{};  ///< What the game holds beside the actions, for its
                                                         ///< own rules on the server to read: which item a
                                                         ///< player's cursor is over, say; a one-off, a move,
                                                         ///< is a message (NetSession::sendMessage). The
                                                         ///< engine gives the bytes no meaning; payloadSize
                                                         ///< of them travel.
    uint8_t                                  payloadSize = 0;
};

} // namespace Vkm::Engine
