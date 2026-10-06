#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "net/wire/bit_stream.h"

namespace Vkm::Engine::Quantize {

/**
 * @brief How finely a replicated position is carried, in metres.
 *
 * Below what a player can see, and coarse enough for twenty-bit coordinates.
 */
constexpr float POSITION_STEP = 0.001f;

/**
 * @brief The fastest a replicated body is described as moving.
 *
 * Past this the value is clamped and the body is drawn slightly slow for a tick.
 */
constexpr float MAX_SPEED = 200.0f;

/**
 * @brief How far from the origin a replicated coordinate may be.
 *
 * A body past it is described at the boundary; see writePosition. A constant,
 * not a parameter: the field's *width* derives from it, and both ends must agree.
 */
constexpr float WORLD_EXTENT = 512.0f;

/**
 * @brief Bits one coordinate occupies on the wire.
 *
 * @return Bit width, sized so the whole of WORLD_EXTENT fits at POSITION_STEP.
 */
constexpr uint32_t positionBits() {
    uint32_t bits  = 1;
    float    range = 2.0f;
    const float need = (WORLD_EXTENT * 2.0f) / POSITION_STEP;
    while (range < need && bits < 32u) {
        range *= 2.0f;
        ++bits;
    }
    return bits;
}

/**
 * @brief Bits one smallest-three rotation component takes.
 *
 * About a fifteenth of a degree at worst; an angle error grows with the size
 * of the thing turned.
 */
constexpr uint32_t ROTATION_BITS = 11;

/// Bits one velocity component takes, across +/-MAX_SPEED.
constexpr uint32_t VELOCITY_BITS = 16;

/**
 * @brief The code for @p value in [-@p range, @p range], @p width bits wide,
 *        with zero exact.
 *
 * Zero must be exact: off by half a step, a rotation tilts, a body at rest
 * drifts and a stick creeps. Past the range a value is clamped; NaN is zero.
 *
 * @param value The value to quantise.
 * @param range Magnitude spanned either side of zero.
 * @param width Bits.
 * @return The code, ready for BitWriter::bits with @p width.
 */
uint32_t toSigned(float value, float range, uint32_t width);

/// Read back what toSigned wrote, bounded to the same range.
float fromSigned(uint32_t raw, float range, uint32_t width);

/**
 * @brief Write @p value, a coordinate within WORLD_EXTENT of the origin.
 *
 * Clamped to +/-WORLD_EXTENT, not refused: a body out of the world is a game
 * bug, not a reason to lose the packet. Explicit, since @ref positionBits
 * overshoots the world. A non-finite coordinate is written as the origin.
 *
 * @param out   The packet.
 * @param value One coordinate, in metres.
 */
void writePosition(BitWriter& out, float value);

/// Read back what writePosition wrote, bounded to the same range.
float readPosition(BitReader& in);

/**
 * @brief Write a rotation as its three smallest components.
 *
 * The largest is recovered from the other three and a two-bit index; it is
 * made positive, since q and -q are the same rotation.
 *
 * @param out   The packet.
 * @param value The rotation; normalised before it is written.
 */
void writeRotation(BitWriter& out, const glm::quat& value);

/// Read back what writeRotation wrote, normalised.
glm::quat readRotation(BitReader& in);

/// Write a velocity component, clamped to MAX_SPEED.
void writeVelocity(BitWriter& out, float value);

/// Read back what writeVelocity wrote.
float readVelocity(BitReader& in);

} // namespace Vkm::Engine::Quantize
