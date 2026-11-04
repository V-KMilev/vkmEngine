#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_network/bit_stream.h"
#include "vkm_network/quantize.h"

namespace Vkm::Engine {

constexpr uint32_t NET_AXIS_BITS = 8;  ///< Bits an axis is carried in. 1/127 of full deflection either side
                                       ///< of an exact center (Network::toSigned), finer than a stick
                                       ///< reports.

} // namespace Vkm::Engine

namespace Vkm::Engine::Quantize {

constexpr float POSITION_STEP = 0.001f;  ///< How finely a replicated position is carried, in meters. Below
                                         ///< what a player can see, and coarse enough for twenty-bit
                                         ///< coordinates.

constexpr float MAX_SPEED = 200.0f;  ///< The fastest a replicated body is described as moving. Past this the
                                     ///< value is clamped and the body is drawn slightly slow for a tick.

constexpr float WORLD_EXTENT = 512.0f;  ///< How far from the origin a replicated coordinate may be; a body
                                        ///< past it is described at the boundary (writePosition). A constant,
                                        ///< not a parameter: the field's width derives from it, and both ends
                                        ///< must agree.

/**
 * @brief Bits one coordinate occupies on the wire.
 *
 * @return Bit width, sized so the whole of WORLD_EXTENT fits at POSITION_STEP.
 */
constexpr uint32_t positionBits() { return Network::fixedBits(WORLD_EXTENT * 2.0f, POSITION_STEP); }

constexpr uint32_t ROTATION_BITS = 11;  ///< Bits one smallest-three rotation component takes. About an
                                        ///< eighth of a degree at worst (0.115, measured); an angle error
                                        ///< grows with the size of the thing turned.

constexpr uint32_t VELOCITY_BITS = 16;  ///< Bits one velocity component takes, across +/-MAX_SPEED.

/**
 * @brief Write @p value, a coordinate within WORLD_EXTENT of the origin.
 *
 * Clamped to +/-WORLD_EXTENT, not refused: a body out of the world is a game
 * bug, not a reason to lose the packet. Explicit, since @ref positionBits
 * overshoots the world. A non-finite coordinate is written as the origin.
 *
 * @param out   The packet.
 * @param value One coordinate, in meters.
 */
void writePosition(Network::BitWriter& out, float value);

/// Read back what writePosition wrote, bounded to the same range.
float readPosition(Network::BitReader& in);

/**
 * @brief Write a rotation as its three smallest components, ROTATION_BITS each.
 *
 * @param out   The packet.
 * @param value The rotation; normalized before it is written.
 */
void writeRotation(Network::BitWriter& out, const glm::quat& value);

/// Read back what writeRotation wrote, normalized.
glm::quat readRotation(Network::BitReader& in);

/// Write a velocity component, clamped to MAX_SPEED.
void writeVelocity(Network::BitWriter& out, float value);

/// Read back what writeVelocity wrote.
float readVelocity(Network::BitReader& in);

} // namespace Vkm::Engine::Quantize
