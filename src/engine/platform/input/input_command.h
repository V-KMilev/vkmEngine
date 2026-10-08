#pragma once

#include <array>
#include <cstdint>

#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief Most actions one command can carry.
 *
 * Fixed size, so a command copies and goes on a wire without a heap allocation per tick. An
 * action past the cap has no slot, which InputMap logs.
 */
inline constexpr uint32_t MAX_INPUT_ACTIONS = 32;

/// Bytes of a game's own a command can carry (InputCommand::payload).
inline constexpr uint32_t MAX_COMMAND_PAYLOAD = 16;

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

    /// Per-action value, -1..1, indexed by InputMap::indexOf.
    std::array<float, MAX_INPUT_ACTIONS> axis{};

    uint32_t pressed  = 0;  ///< Bit per action: became active since the last command.
    uint32_t released = 0;  ///< Bit per action: stopped being active since the last command.

    static_assert(
        MAX_INPUT_ACTIONS <= 32,
        "pressed / released carry one bit per action in a uint32_t, "
        "so an action past bit 31 has nowhere to be recorded"
    );

    /**
     * @brief Where the player was looking when this input was taken.
     *
     * Carried so a tick's movement depends on its command alone, not on a camera that has since
     * moved. A quaternion, so there is no Euler convention; identity (looking down -Z) until set.
     */
    glm::quat view{1.0f, 0.0f, 0.0f, 0.0f};

    /// What the game says beside the actions, for its own rules on the server to read: a chess
    /// move, a choice in a lobby. The engine gives the bytes no meaning; payloadSize of them travel.
    std::array<uint8_t, MAX_COMMAND_PAYLOAD> payload{};
    uint8_t payloadSize = 0;
};

} // namespace Vkm::Engine
