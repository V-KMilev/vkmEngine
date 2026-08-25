#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"

namespace Vkm::Engine::Math {

/**
 * @brief Rotate the forward basis by the quaternion and normalize.
 *
 * Forward is -Z; see axes.h for why, and for what it cost when it was not.
 */
inline glm::vec3 computeForward(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_FORWARD);
}

/**
 * @brief Rotate the +Y basis by the quaternion and normalize.
 */
inline glm::vec3 computeUp(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_AXIS_Y);
}

/**
 * @brief Rotate the right basis by the quaternion and normalize.
 *
 * Right is +X, and it is the plain answer only because forward is -Z: a
 * right-handed basis needs right x up = -forward, and -Z forward with +Y up
 * gives exactly +X. The engine's previous +Z forward made this -X, which is
 * what inverted the fly camera's strafe, the stress arena's, and the physics
 * lab walker's in turn.
 */
inline glm::vec3 computeRight(const glm::quat& rotation) {
    return glm::normalize(rotation * WORLD_RIGHT);
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

/**
 * @brief Rotation that faces @p direction, with @p up as the reference upright.
 *
 * A pass-through: forward is -Z, which is what glm::quatLookAt builds for, so
 * there is nothing to correct. It stays because the engine's forward is a
 * decision rather than glm's, and a caller asking for "face this" should not
 * have to know the two currently agree.
 *
 * @param direction Direction to face; need not be normalized, but must not be
 *        zero, and must not be parallel to @p up.
 * @param up Reference upright, world up unless the caller means otherwise.
 * @return The orientation whose forward is @p direction.
 */
inline glm::quat lookRotation(const glm::vec3& direction,
                              const glm::vec3& up = {0.0f, 1.0f, 0.0f}) {
    return glm::quatLookAt(glm::normalize(direction), up);
}

/**
 * @brief Rotation from a yaw about world up and a pitch about the axis across it.
 *
 * The parameterisation a look control uses: yaw 0 and pitch 0 face forward,
 * a rising pitch raises the view, and a rising yaw turns left - the last
 * because right is +X and a positive turn about +Y goes the other way.
 *
 * Here rather than in the camera because a camera that builds this from raw
 * axes is a second copy of the convention, and a change of convention walks
 * straight past it: the helpers move, the hand-rolled version does not, and
 * the view pitches the wrong way against a mouse that has not changed.
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
 * Its exact inverse, and that is the point of it being written beside it. A
 * mapping and a hand-derived reversal of it in separate functions drift the
 * moment either moves: the camera flies correctly and jumps the instant
 * anything re-derives its angles, which is the kind of fault that survives a
 * quick try because the first thing you do still works.
 *
 * @param direction Direction to face; need not be normalized.
 * @param[out] yaw Radians about world up.
 * @param[out] pitch Radians above the horizon.
 */
inline void toYawPitch(const glm::vec3& direction, float& yaw, float& pitch) {
    const glm::vec3 d = glm::normalize(direction);
    pitch = std::asin(glm::clamp(d.y, -1.0f, 1.0f));
    // Negated because forward is -Z: fromYawPitch sends yaw 0 to -Z, so the
    // angle is measured from there rather than from +Z.
    yaw = std::atan2(-d.x, -d.z);
}

} // namespace Vkm::Engine::Math
