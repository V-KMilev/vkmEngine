#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_operation.hpp>

#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "system/physics/collision/part_bounds.h"

namespace Vkm::Engine {

/**
 * @brief Inertia tensor of a solid box about its centre, in body-local space.
 *
 * I_x = (1/12) m (h_y^2 + h_z^2) with full extents h = 2*halfExtents.
 *
 * @param mass Total mass, kg.
 * @param halfExtents Half the box's size on each local axis.
 * @return The tensor; mat3(0) for a non-positive mass or a degenerate extent.
 */
inline glm::mat3 boxInertiaLocal(float mass, const glm::vec3& halfExtents) {
    if (mass <= 0.0f) return glm::mat3(0.0f);

    const glm::vec3 full = halfExtents * 2.0f;
    const float k = mass / 12.0f;
    const float ix = k * (full.y * full.y + full.z * full.z);
    const float iy = k * (full.x * full.x + full.z * full.z);
    const float iz = k * (full.x * full.x + full.y * full.y);
    if (ix <= 0.0f || iy <= 0.0f || iz <= 0.0f) return glm::mat3(0.0f);

    return glm::diagonal3x3(glm::vec3(ix, iy, iz));
}

/**
 * @brief Inertia tensor of a solid capsule about its centre, body-local, segment along local +Y.
 *
 * A cylinder plus two hemispherical caps, weighted by volume. Not the enclosing box's: an upright
 * capsule's axis inertia is several times smaller, the difference between a graze spinning it or not.
 *
 * @param mass Total mass, kg.
 * @param radius Sweep radius; also the cap radius.
 * @param halfHeight Half the segment length, caps excluded.
 * @return The tensor; mat3(0) for a non-positive mass or radius.
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

    // The 3*h*r/8 term: hemisphere centroids sit 3r/8 past each cylinder end (parallel-axis shift).
    const float axial = cylinderMass * r2 * 0.5f + capsMass * (2.0f / 5.0f) * r2;
    const float perp  = cylinderMass * (h * h / 12.0f + r2 * 0.25f)
        + capsMass * ((2.0f / 5.0f) * r2 + h * h * 0.25f + 3.0f * h * radius / 8.0f);
    if (axial <= 0.0f || perp <= 0.0f) return glm::mat3(0.0f);

    return glm::diagonal3x3(glm::vec3(perp, axial, perp));
}

/**
 * @brief Rotate a body-local inverse inertia tensor into world space.
 *
 * I_world^-1 = R * I_local^-1 * R^T.
 *
 * @param invInertiaLocal Body-local inverse inertia tensor.
 * @param rotation The body's world orientation.
 * @return The world-space inverse inertia tensor.
 */
inline glm::mat3 inverseInertiaWorld(const glm::mat3& invInertiaLocal, const glm::quat& rotation) {
    const glm::mat3 r = glm::mat3_cast(rotation);
    return r * invInertiaLocal * glm::transpose(r);
}

/**
 * @brief How a body's mass lies, from its collider: a lone capsule is that capsule, anything else a
 *        solid box over the collider's bound (docs/reference/physics.md).
 *
 * Its centre and its inertia both come from here, so they cannot disagree.
 */
struct MassModel {
    glm::vec3 centre{0.0f};        ///< Where the mass is centred: from the body's origin, in its frame.
    bool      capsule    = false;  ///< A capsule, not a box.
    float     radius     = 0.0f;   ///< The capsule's.
    float     halfHeight = 0.0f;   ///< The capsule's.
    glm::vec3 halfExtents{0.0f};   ///< The box's; zero with no collider.

    /// @p collider's, or a point at the origin with none.
    static MassModel of(const Collider* collider) {
        MassModel model;
        if (!collider || collider->parts.empty()) return model;
        const ColliderPart& first = collider->parts[0];
        if (collider->parts.size() == 1 && first.shape == ColliderShape::Capsule) {
            model.centre     = first.center;
            model.capsule    = true;
            model.radius     = first.radius;
            model.halfHeight = first.halfHeight;
            return model;
        }
        const Math::AABB bounds = colliderLocalBounds(*collider);
        model.centre      = (bounds.max + bounds.min) * 0.5f;
        model.halfExtents = (bounds.max - bounds.min) * 0.5f;
        return model;
    }

    /// Its inertia tensor about its centre for @p mass kilograms, body-local; mat3(0) for none.
    glm::mat3 inertia(float mass) const {
        return capsule ? capsuleInertiaLocal(mass, radius, halfHeight) : boxInertiaLocal(mass, halfExtents);
    }
};

/// 1/mass of a Dynamic body of positive mass, asleep or not; zero for any other.
inline float inverseMassOf(const Rigidbody& rb, RigidbodyMotion motion) {
    return motion == RigidbodyMotion::Dynamic && rb.mass > 0.0f ? 1.0f / rb.mass : 0.0f;
}

/**
 * @brief Where the solver holds a body, in its own frame: its centre of mass when Dynamic with mass
 *        (inverseMassOf above zero), asleep or another end's to move too; its origin for any other.
 *
 * The point its velocity is of, and it turns about.
 *
 * @param rb       The body.
 * @param motion   How it moves this tick, which a posed ragdoll bone overrides.
 * @param collider Its collider, or null.
 * @return The point, from the body's origin.
 */
inline glm::vec3 solverCentre(const Rigidbody& rb, RigidbodyMotion motion, const Collider* collider) {
    return inverseMassOf(rb, motion) > 0.0f ? MassModel::of(collider).centre : glm::vec3(0.0f);
}

} // namespace Vkm::Engine
