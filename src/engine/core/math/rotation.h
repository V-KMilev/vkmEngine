#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"

namespace Engine::Math {

/**
 * @brief Rotate the +Z basis by the quaternion and normalize.
 */
inline glm::vec3 computeForward(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_Z);
}

/**
 * @brief Rotate the +Y basis by the quaternion and normalize.
 */
inline glm::vec3 computeUp(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_Y);
}

/**
 * @brief Rotate the -X basis by the quaternion and normalize.
 *
 * The negation is deliberate. Forward is +Z and the maths is right-handed, so a
 * right-handed camera basis (right x up = -forward) puts right at -X, not +X.
 * Returning the raw +X axis here is what inverted the fly camera's strafe; see
 * axes.h.
 */
inline glm::vec3 computeRight(const glm::quat& rotation) {
    return glm::normalize(rotation * -WORLD_AXIS_X);
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
