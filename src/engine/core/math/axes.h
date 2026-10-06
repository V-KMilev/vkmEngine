#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief World basis vectors.
 *
 * Right-handed, +Y up, **forward is -Z** (glm's and OpenGL view space's
 * convention), so screen-right is +X: `glm::cross(forward, up)` gives it, and
 * `glm::quatLookAt` needs no negation. +Z forward would put right at -X and
 * silently mirror the intuitive code.
 *
 * Use these only as axes; ask about an orientation with computeForward /
 * computeRight / computeUp.
 */
inline const glm::vec3 WORLD_AXIS_X = {1.0f, 0.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Y = {0.0f, 1.0f, 0.0f};
inline const glm::vec3 WORLD_AXIS_Z = {0.0f, 0.0f, 1.0f};

// Written out, not copied from the axes: glm is not constexpr with intrinsics, and
// Clang on Windows would run the copy before the axis is set.
inline const glm::vec3 WORLD_UP      = {0.0f, 1.0f,  0.0f};  ///< Up is +Y.
inline const glm::vec3 WORLD_FORWARD = {0.0f, 0.0f, -1.0f};  ///< Forward is -Z.
inline const glm::vec3 WORLD_RIGHT   = {1.0f, 0.0f,  0.0f};  ///< Screen-right is +X.

} // namespace Vkm::Engine::Math
