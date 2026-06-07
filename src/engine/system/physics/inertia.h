#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {

/**
 * @brief Inverse inertia tensor of a solid box in body-local space.
 *
 * Solid box about its centre: I_x = (1/12) m (h_y^2 + h_z^2) using full
 * extents h = 2 * halfExtents. A non-positive mass or degenerate extent yields
 * mat3(0).
 */
inline glm::mat3 boxInverseInertiaLocal(float mass, const glm::vec3& halfExtents) {
    if (mass <= 0.0f) return glm::mat3(0.0f);

    const glm::vec3 full = halfExtents * 2.0f;
    const float k = mass / 12.0f;
    const float ix = k * (full.y * full.y + full.z * full.z);
    const float iy = k * (full.x * full.x + full.z * full.z);
    const float iz = k * (full.x * full.x + full.y * full.y);
    if (ix <= 0.0f || iy <= 0.0f || iz <= 0.0f) return glm::mat3(0.0f);

    return glm::mat3(
        1.0f / ix, 0.0f, 0.0f,
        0.0f, 1.0f / iy, 0.0f,
        0.0f, 0.0f, 1.0f / iz
    );
}

/**
 * @brief Rotate a body-local inverse inertia tensor into world space.
 *
 * I_world^-1 = R * I_local^-1 * R^T, with R the rotation matrix of the body's
 * orientation. Recomputed each tick because the orientation changes.
 */
inline glm::mat3 inertiaWorld(const glm::mat3& invInertiaLocal, const glm::quat& rotation) {
    const glm::mat3 r = glm::mat3_cast(rotation);
    return r * invInertiaLocal * glm::transpose(r);
}

} // namespace Engine
