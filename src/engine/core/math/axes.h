#pragma once

#include <glm/glm.hpp>

namespace Engine::Math {

/**
 * @brief World basis vectors.
 *
 * The engine's forward is +Z (see computeForward), and the maths is GLM's, which
 * is right-handed. Those two facts together decide what "right" means, and the
 * answer is not the obvious one:
 *
 *   a right-handed camera basis needs  right x up = -viewDirection
 *   with viewDirection = +Z and up = +Y that gives  right = -X
 *
 * So for anything facing the engine's forward, **screen-right is -X**. The axis
 * constants below are therefore named for the axis and not for a direction -
 * a previous WORLD_AXIS_X_RIGHT claimed +X was right, which is false here and
 * inverted the fly camera's strafe when it was believed. Use computeRight() when
 * you want the actual right-hand direction of an orientation; use these only as
 * axes, e.g. the axis of a rotation.
 */
inline const glm::vec3 WORLD_AXIS_X = {1.0f, 0.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Y = {0.0f, 1.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Z = {0.0f, 0.0f, 1.0f};

/// Up is +Y, and forward is +Z. Named aliases for the two that carry a meaning
/// beyond "an axis", so call sites read as intent rather than as a coordinate.
inline const glm::vec3 WORLD_UP      = WORLD_AXIS_Y;
inline const glm::vec3 WORLD_FORWARD = WORLD_AXIS_Z;

} // namespace Engine::Math
