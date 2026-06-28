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

/**
 * @brief Rotation of a world/model matrix, scale-tolerant.
 *
 * Normalises the upper-3x3 basis columns so a uniformly/non-uniformly scaled
 * matrix still yields the correct rotation, then quat_casts the orthonormal basis.
 */
inline glm::quat worldRotationOf(const glm::mat4& worldMatrix) {
    glm::mat3 basis(worldMatrix);
    basis[0] = glm::normalize(basis[0]);
    basis[1] = glm::normalize(basis[1]);
    basis[2] = glm::normalize(basis[2]);
    return glm::normalize(glm::quat_cast(basis));
}

} // namespace Engine::Math
