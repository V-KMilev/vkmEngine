#pragma once

namespace Vkm::Engine::Physics {

/**
 * @brief The tolerances collision and character movement are decided by.
 *
 * Gathered because they were scattered: four names and seven bare literals
 * across the subsystem, with no stated relationship between any of them, so
 * nothing said whether two 1e-4 were the same decision or a coincidence.
 *
 * None of these is `glm::epsilon`, and the distinction is worth keeping. That
 * one answers "can this float be divided by", which is a question about the
 * format. These answer "is this geometry too small to mean anything", which is
 * a question about the world being simulated - a capsule shorter than a
 * millimetre is a point because nothing in a game cares about the difference,
 * not because the arithmetic would fail. Their units differ and are stated:
 * mixing a squared length with a length is how one of them ends up an eighth of
 * what its author thought.
 */

/**
 * @brief Squared length below which a segment, axis or direction is a point.
 *
 * 1e-6 of a squared metre is a millimetre of length. Below it a capsule is a
 * sphere, an edge has no direction, and a division by its length is one by
 * nothing - so the three cases share a threshold rather than each picking one.
 */
inline constexpr float DEGENERATE_SQ = 1e-6f;

/**
 * @brief How near two quantities must be to count as the same, in metres.
 *
 * A tenth of a millimetre: far below anything a player can see or a solver
 * needs to resolve, and far above where single precision gets unreliable at the
 * scale of a level. Used where an iteration has to be told it has arrived.
 */
inline constexpr float CONTACT_TOLERANCE = 1e-4f;

/**
 * @brief How square-on a normal must be before it counts as axis-aligned.
 *
 * Unitless, on a dot product against a unit axis. Box faces are exactly
 * axis-aligned in their own frame and arrive here through a matrix, so this
 * absorbs the rounding of that trip and nothing else.
 */
inline constexpr float AXIS_TOLERANCE = 1e-4f;

/**
 * @brief Smallest half-extent a fitted collider is given, in metres.
 *
 * A millimetre. A box fitted to flat geometry would otherwise be a plane with
 * no thickness, which has no inside for the solver to push out of.
 */
inline constexpr float MIN_HALF_EXTENT = 1e-3f;

/**
 * @brief Relative speed below which a contact has no direction to rub along,
 *        in metres per second.
 *
 * A micrometre a second. Friction needs a tangent, and a tangent is only
 * meaningful while there is sliding to resist: below this the direction is
 * whatever the last bit of arithmetic rounded to, and normalising it points the
 * impulse somewhere arbitrary.
 *
 * A speed, not a length, and named separately for that reason - it read
 * CONTACT_TOLERANCE before, which is a distance, and mixing the two is the
 * mistake this file exists to make visible.
 */
inline constexpr float FRICTION_DEADZONE = 1e-6f;

} // namespace Vkm::Engine::Physics
