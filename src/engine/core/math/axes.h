#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief World basis vectors.
 *
 * The engine is right-handed, +Y up, and **forward is -Z** - glm's own
 * convention, and OpenGL's view space, which is the space the renderer already
 * works in. That makes screen-right +X: `glm::cross(forward, up)` gives it,
 * and `glm::quatLookAt` faces what it is given without a negation.
 *
 * The alternative, +Z forward, is the pairing Unity uses - but Unity earns it
 * by being left-handed. Right-handed with +Z forward is the one combination
 * that puts right at -X, and the cost is not that it is unusual: it makes the
 * intuitive line of code silently produce a mirrored answer, which is how a
 * control ships reversed and reads correct.
 *
 * The constants below are named for the axis rather than for a direction. Use
 * computeForward / computeRight / computeUp to ask about an orientation, and
 * these only as axes - the axis of a rotation, say.
 */
inline const glm::vec3 WORLD_AXIS_X = {1.0f, 0.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Y = {0.0f, 1.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Z = {0.0f, 0.0f, 1.0f};

inline const glm::vec3 WORLD_UP      = WORLD_AXIS_Y;   ///< Up is +Y.
inline const glm::vec3 WORLD_FORWARD = -WORLD_AXIS_Z;  ///< Forward is -Z.
inline const glm::vec3 WORLD_RIGHT   = WORLD_AXIS_X;   ///< Screen-right is +X.

} // namespace Vkm::Engine::Math
