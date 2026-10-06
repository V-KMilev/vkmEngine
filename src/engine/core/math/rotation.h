#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"

namespace Vkm::Engine::Math {

/**
 * @brief Rotate the forward basis by the quaternion and normalize.
 *
 * Forward is -Z; see axes.h for why.
 *
 * @param rotation Orientation to read.
 * @return Its unit forward vector.
 */
inline glm::vec3 computeForward(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_FORWARD);
}

/**
 * @brief Rotate the +Y basis by the quaternion and normalize.
 *
 * @param rotation Orientation to read.
 * @return Its unit up vector.
 */
inline glm::vec3 computeUp(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_Y);
}

/**
 * @brief Rotate the right basis by the quaternion and normalize.
 *
 * Right is +X because forward is -Z: right-handed, right x up = -forward.
 *
 * @param rotation Orientation to read.
 * @return Its unit right vector.
 */
inline glm::vec3 computeRight(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_RIGHT);
}

/**
 * @brief The rotation a model matrix was composed with, whatever its scale.
 *
 * Transform::fromModelMatrix uses it, so the two agree. A mirror is taken as a
 * flip on X, where fromModelMatrix carries it as negative scale. A zero-scaled
 * axis is implied by the other two, or the identity's column, so never NaN.
 *
 * @param worldMatrix Model matrix to read; translation is ignored.
 * @return Its rotation, normalised.
 */
inline glm::quat worldRotationOf(const glm::mat4& worldMatrix) {
    glm::mat3 basis(worldMatrix);
    const bool mirrored = glm::determinant(basis) < 0.0f;

    for (int axis = 0; axis < 3; ++axis) {
        const float length = glm::length(basis[axis]);
        basis[axis] = length != 0.0f ? basis[axis] / length : glm::vec3(0.0f);
    }
    // Otherwise quat_cast reads the reflection as a rotation.
    if (mirrored) basis[0] = -basis[0];

    for (int axis = 0; axis < 3; ++axis) {
        if (glm::dot(basis[axis], basis[axis]) > 0.0f) continue;

        const glm::vec3 implied = glm::cross(basis[(axis + 1) % 3], basis[(axis + 2) % 3]);
        basis[axis] = glm::dot(implied, implied) > 0.0f ? implied : glm::mat3(1.0f)[axis];
    }

    return glm::normalize(glm::quat_cast(basis));
}

/**
 * @brief Rotation that faces @p direction, with @p up as the reference upright.
 *
 * @param direction Direction to face; need not be normalized, but must be
 *        non-zero and not parallel to @p up.
 * @param up Reference upright.
 * @return The orientation whose forward is @p direction.
 */
inline glm::quat lookRotation(const glm::vec3& direction, const glm::vec3& up = {0.0f, 1.0f, 0.0f}) {
    return glm::quatLookAt(glm::normalize(direction), up);
}

/**
 * @brief Rotation from a yaw about world up and a pitch about the axis across it.
 *
 * Zero faces forward; rising pitch looks up, rising yaw turns left (right is
 * +X, and a positive turn about +Y goes the other way).
 *
 * @param yaw Radians about world up.
 * @param pitch Radians above the horizon; the caller clamps it.
 * @return The orientation those two angles name.
 */
inline glm::quat fromYawPitch(float yaw, float pitch) {
    return glm::angleAxis(yaw, WORLD_AXIS_Y) * glm::angleAxis(pitch, WORLD_AXIS_X);
}

/**
 * @brief The yaw and pitch fromYawPitch needs to produce @p direction.
 *
 * Its exact inverse.
 *
 * @param direction Direction to face; need not be normalized.
 * @param[out] yaw Radians about world up.
 * @param[out] pitch Radians above the horizon.
 */
inline void toYawPitch(const glm::vec3& direction, float& yaw, float& pitch) {
    const glm::vec3 d = glm::normalize(direction);
    pitch = std::asin(glm::clamp(d.y, -1.0f, 1.0f));
    // Negated: yaw 0 is -Z, so the angle is measured from there.
    yaw = std::atan2(-d.x, -d.z);
}

} // namespace Vkm::Engine::Math
