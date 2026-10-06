#pragma once

namespace Vkm::Engine::Physics {

// Not `glm::epsilon`, which asks whether a float can be divided by: these ask whether geometry is too
// small to mean anything in the world. Units are stated.

/**
 * @brief Squared length below which a segment, axis or direction is a point.
 *
 * A millimetre of length. Below it a capsule is a sphere and an edge has no direction.
 */
inline constexpr float DEGENERATE_SQ = 1e-6f;

/**
 * @brief How near two quantities must be to count as the same, in metres.
 *
 * A tenth of a millimetre: below anything visible, above single precision's noise at level scale.
 */
inline constexpr float CONTACT_TOLERANCE = 1e-4f;

/**
 * @brief How square-on a normal must be before it counts as axis-aligned.
 *
 * Unitless, on a dot against a unit axis; absorbs only the rounding of a box face through a matrix.
 */
inline constexpr float AXIS_TOLERANCE = 1e-4f;

/**
 * @brief Smallest half-extent a fitted collider is given, in metres.
 *
 * A millimetre, so a box fitted to flat geometry has an inside to push out of.
 */
inline constexpr float MIN_HALF_EXTENT = 1e-3f;

/**
 * @brief Friction impulse, in newton-seconds, below which the friction-cone clamp is skipped.
 *
 * The clamp divides by the impulse's magnitude.
 */
inline constexpr float FRICTION_DEADZONE = 1e-6f;

} // namespace Vkm::Engine::Physics
