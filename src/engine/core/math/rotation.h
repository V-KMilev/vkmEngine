#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"

namespace Engine::Math {

/**
 * @brief Rotate the +Z basis by the quaternion and normalize.
 */
inline glm::vec3 computeForward(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_Z_FORWARD);
}

/**
 * @brief Rotate the +Y basis by the quaternion and normalize.
 */
inline glm::vec3 computeUp(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_Y_UP);
}

/**
 * @brief Rotate the +X basis by the quaternion and normalize.
 */
inline glm::vec3 computeRight(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_X_RIGHT);
}

} // namespace Engine::Math
