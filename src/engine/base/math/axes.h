#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

// The convention is architecture.md's "Axes": right-handed, +Y up, forward -Z (glm's).
inline const glm::vec3 WORLD_UP      = {0.0f, 1.0f,  0.0f};  ///< Up is +Y.
inline const glm::vec3 WORLD_FORWARD = {0.0f, 0.0f, -1.0f};  ///< Forward is -Z.
inline const glm::vec3 WORLD_RIGHT   = {1.0f, 0.0f,  0.0f};  ///< Screen-right is +X: cross(forward, up).

/**
 * @brief An up axis for a light looking along @p dir, never parallel to it.
 *
 * So a near-vertical light never makes a degenerate lookAt.
 *
 * @param dir The direction the light looks along, normalized.
 * @return World +Y, or +Z when @p dir is within a few degrees of vertical.
 */
inline glm::vec3 stableUp(const glm::vec3& dir) {
    return std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace Vkm::Engine::Math
