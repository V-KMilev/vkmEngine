#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief Inertia tensor of a solid box about its centre, in body-local space.
 *
 * Solid box: I_x = (1/12) m (h_y^2 + h_z^2) using full extents h = 2*halfExtents.
 * Returns mat3(0) for a non-positive mass or degenerate extent. Callers that need
 * the inverse (and may parallel-axis-shift first) invert the result themselves.
 */
inline glm::mat3 boxInertiaLocal(float mass, const glm::vec3& halfExtents) {
    if (mass <= 0.0f) return glm::mat3(0.0f);

    const glm::vec3 full = halfExtents * 2.0f;
    const float k = mass / 12.0f;
    const float ix = k * (full.y * full.y + full.z * full.z);
    const float iy = k * (full.x * full.x + full.z * full.z);
    const float iz = k * (full.x * full.x + full.y * full.y);
    if (ix <= 0.0f || iy <= 0.0f || iz <= 0.0f) return glm::mat3(0.0f);

    return glm::mat3(
        ix, 0.0f, 0.0f,
        0.0f, iy, 0.0f,
        0.0f, 0.0f, iz
    );
}

/**
 * @brief Inertia tensor of a solid capsule about its centre, in body-local space,
 *        with the segment along local +Y.
 *
 * A cylinder of height 2*halfHeight plus two hemispherical caps, each part
 * weighted by its share of the volume. Returns mat3(0) for a non-positive mass
 * or a non-positive radius. The capsule cannot be approximated by the box of its
 * extent the way the compound path does: an upright character capsule is the one
 * shape whose axis inertia (a thin cylinder) is several times smaller than the
 * enclosing box's, which is the difference between a graze spinning it and not.
 *
 * @param mass Total mass of the capsule, in kg.
 * @param radius Sweep radius; also the cap radius.
 * @param halfHeight Half the segment length, caps excluded.
 * @return Body-local inertia tensor about the capsule centre.
 */
inline glm::mat3 capsuleInertiaLocal(float mass, float radius, float halfHeight) {
    if (mass <= 0.0f || radius <= 0.0f) return glm::mat3(0.0f);

    const float h = glm::max(halfHeight, 0.0f) * 2.0f;
    const float r2 = radius * radius;
    const float cylinderVolume = glm::pi<float>() * r2 * h;
    const float capsVolume     = (4.0f / 3.0f) * glm::pi<float>() * r2 * radius;
    const float total          = cylinderVolume + capsVolume;
    if (total <= 0.0f) return glm::mat3(0.0f);

    const float cylinderMass = mass * (cylinderVolume / total);
    const float capsMass     = mass * (capsVolume / total);

    // The caps' perpendicular term is the parallel-axis shift of two hemispheres
    // seated on the cylinder ends: 3*h*r/8 is the hemisphere centroid offset.
    const float axial = cylinderMass * r2 * 0.5f + capsMass * (2.0f / 5.0f) * r2;
    const float perp  = cylinderMass * (h * h / 12.0f + r2 * 0.25f)
                      + capsMass * ((2.0f / 5.0f) * r2 + h * h * 0.25f + 3.0f * h * radius / 8.0f);
    if (axial <= 0.0f || perp <= 0.0f) return glm::mat3(0.0f);

    return glm::mat3(
        perp, 0.0f, 0.0f,
        0.0f, axial, 0.0f,
        0.0f, 0.0f, perp
    );
}

/**
 * @brief Parallel-axis shift of an inertia tensor from the centre of mass to a
 *        parallel axis offset by @p offset: I' = I + m (|d|^2 E - d d^T).
 *
 * Used when a collider's centre is offset from the entity origin (where the
 * solver measures contact arms) so the body rotates about the correct axis.
 */
inline glm::mat3 parallelAxisShift(const glm::mat3& inertia, float mass, const glm::vec3& offset) {
    const float d2 = glm::dot(offset, offset);
    return inertia + mass * (glm::mat3(d2) - glm::outerProduct(offset, offset));
}

/**
 * @brief Rotate a body-local inverse inertia tensor into world space.
 *
 * I_world^-1 = R * I_local^-1 * R^T, with R the rotation matrix of the body's
 * orientation. Recomputed each tick because the orientation changes.
 */
inline glm::mat3 inverseInertiaWorld(const glm::mat3& invInertiaLocal, const glm::quat& rotation) {
    const glm::mat3 r = glm::mat3_cast(rotation);
    return r * invInertiaLocal * glm::transpose(r);
}

} // namespace Vkm::Engine
