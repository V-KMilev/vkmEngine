#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "net/wire/bit_stream.h"

namespace Vkm::Engine::Quantize {

/**
 * @brief How finely a replicated position is carried, in metres.
 *
 * A millimetre. Below what a player can see at any camera distance a game puts
 * them at, and coarse enough that a world a kilometre across fits a position in
 * three twenty-bit fields rather than three floats - which is the difference
 * between twenty bodies in a packet and eleven.
 */
constexpr float POSITION_STEP = 0.001f;

/**
 * @brief The fastest a replicated body is described as moving.
 *
 * Past this the value is clamped and the body is drawn slightly slow for a tick, which
 * is invisible and costs a bit rather than a branch.
 */
constexpr float MAX_SPEED = 200.0f;

/**
 * @brief Bits a coordinate needs, for a world @p extent metres from the origin.
 *
 * @param extent Half the width of the world the game plays in.
 * @return Bit width, sized so the whole range fits at POSITION_STEP.
 */
constexpr uint32_t positionBits(float extent) {
    uint32_t bits  = 1;
    float    range = 2.0f;
    const float need = (extent * 2.0f) / POSITION_STEP;
    while (range < need && bits < 32u) { range *= 2.0f; ++bits; }
    return bits;
}

/**
 * @brief Write @p value, a coordinate within @p extent of the origin.
 *
 * Clamped rather than refused. A body that has left the playable world is
 * already a bug in the game, and describing it wrongly at the boundary is a
 * better answer than refusing the whole packet it happened to be in.
 */
void writePosition(BitWriter& out, float value, float extent);

/// Read back what writePosition wrote.
float readPosition(BitReader& in, float extent);

/**
 * @brief Write a rotation as its three smallest components.
 *
 * A unit quaternion has three degrees of freedom, so the largest component is
 * recoverable from the other three and its index - which trades a 32-bit field
 * for two bits and keeps the precision where it is noticed. The sign is folded
 * away by writing the quaternion with its largest component positive, since q
 * and -q are the same rotation.
 */
void writeRotation(BitWriter& out, const glm::quat& value);

/// Read back what writeRotation wrote, normalised.
glm::quat readRotation(BitReader& in);

/// Write a velocity component, clamped to MAX_SPEED.
void writeVelocity(BitWriter& out, float value);

/// Read back what writeVelocity wrote.
float readVelocity(BitReader& in);

} // namespace Vkm::Engine::Quantize
