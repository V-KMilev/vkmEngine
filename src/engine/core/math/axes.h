#pragma once

#include <glm/glm.hpp>

namespace Engine::Math {

/**
 * @brief Standard 3D basis direction vectors in world space.
 *
 * WORLD_AXIS_X_RIGHT   (+X): Right.
 * WORLD_AXIS_Y_UP      (+Y): Up.
 * WORLD_AXIS_Z_FORWARD (+Z): Forward (the engine's forward; see computeForward).
 */
inline const glm::vec3 WORLD_AXIS_X_RIGHT   = {1.0f, 0.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Y_UP      = {0.0f, 1.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Z_FORWARD = {0.0f, 0.0f, 1.0f};

} // namespace Engine::Math
