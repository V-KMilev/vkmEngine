#pragma once

#include <array>
#include <cstdint>

#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief Most actions one command can carry.
 *
 * A command is a fixed-size value so it can be copied, kept and eventually put
 * on a wire without a heap allocation per tick. An action defined past the cap
 * has no slot in the command, which InputMap logs once.
 */
inline constexpr uint32_t MAX_INPUT_ACTIONS = 32;

/**
 * @brief Everything one simulation tick was told to do, as a value.
 *
 * The unit a tick consumes, rather than the live device state a frame samples.
 * Input arrives on the render clock and simulation runs on the tick clock, so
 * reading the device directly from a fixed update drops a tap that began and
 * ended between two ticks, and repeats a press across every tick of a slow
 * frame - once per tick, from one keypress.
 *
 * A command avoids both because it is built per tick from what happened since
 * the previous one: the axes as they stand, and the edges latched across every
 * frame in between. It is also what makes a tick replayable.
 *
 * sequence and tick are separate fields because a replay re-runs old ticks
 * under their original sequence numbers.
 */
struct InputCommand {
    uint32_t sequence = 0;  ///< Monotonic command number; what an ack names.
    uint32_t tick     = 0;  ///< Simulation tick this command drives.

    /// Per-action value, -1..1, indexed by InputMap::indexOf.
    std::array<float, MAX_INPUT_ACTIONS> axis{};

    uint32_t pressed  = 0;  ///< Bit per action: became active since the last command.
    uint32_t released = 0;  ///< Bit per action: stopped being active since the last command.

    static_assert(MAX_INPUT_ACTIONS <= 32,
                  "pressed / released carry one bit per action in a uint32_t, "
                  "so an action past bit 31 has nowhere to be recorded");

    /**
     * @brief Where the player was looking when this input was taken.
     *
     * Axes alone do not say what a tick did with them. "Forward" is forward
     * relative to a view, the view turns on the render clock, and a tick that
     * reads the camera reads whatever the last frame left there - so the same
     * command replayed against a camera that has since moved walks somewhere
     * else. Carrying the orientation here is what makes a tick's movement a
     * function of its command and nothing else.
     *
     * A quaternion rather than a yaw so there is no Euler convention to agree
     * on, and because a look direction is what a server validating a shot needs
     * anyway. Identity until a game sets one, which reads as looking down -Z.
     */
    glm::quat view{1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace Vkm::Engine
