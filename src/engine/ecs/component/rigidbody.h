#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief Dynamics state for a physics body: linear + angular motion, material
 *        response, and mass properties.
 *
 * Data-only component. PhysicsSystem integrates this each fixed tick and writes
 * the resulting pose back to the entity's Transform. A body with inverseMass == 0
 * is static/kinematic (infinite mass): forces never move it, but it still acts as
 * an immovable wall during collision.
 *
 * invInertiaLocal is the inverse inertia tensor in body-local space, derived from
 * the Collider shape + mass (see system/physics/inertia.h). PhysicsSystem
 * recomputes it from mass + Collider at the start of each tick, so editing mass or
 * the shape takes effect without a separate "apply" step; the solver then rotates
 * it into world space.
 *
 * Bodies are assumed to be hierarchy roots so the local Transform equals the world
 * pose. Parenting a physics body is unsupported in this pass.
 */
struct Rigidbody {
    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};  ///< World-space velocity (m/s)
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};  ///< World-space spin (rad/s, axis * speed)

    glm::mat3 invInertiaLocal = glm::mat3(0.0f);     ///< Body-local inverse inertia; 0 = no rotational response

    float mass        = 1.0f;                        ///< Authoring mass in kg; <= 0 is treated as static
    float inverseMass = 1.0f;                        ///< Cached 1/mass; 0 == static/kinematic (infinite mass)

    float linearDamping  = 0.01f;                    ///< Per-second velocity bleed (drag)
    float angularDamping = 0.05f;                    ///< Per-second spin bleed

    float restitution = 0.2f;                        ///< Bounciness [0,1]
    float friction    = 0.5f;                        ///< Coulomb coefficient [0,1+]

    float gravityScale = 1.0f;                        ///< Multiplier on world gravity (0 = floats)

    bool isKinematic = false;                         ///< Script-driven: ignores forces, immune to impulses, still moves
    bool isStatic    = false;                         ///< Never moves; inverseMass forced to 0
    bool sleeping    = false;                         ///< Below energy threshold; skipped until disturbed

    float sleepTimer = 0.0f;                          ///< Runtime-only: seconds spent resting; not persisted
};

// invInertiaLocal / inverseMass are derived from mass + Collider on load;
// sleeping / sleepTimer are runtime-only. All are intentionally absent.
VKM_REFLECT_BEGIN(Rigidbody)
    VKM_F(linearVelocity),
    VKM_F(angularVelocity),
    VKM_F(mass),
    VKM_F(linearDamping),
    VKM_F(angularDamping),
    VKM_F(restitution),
    VKM_F(friction),
    VKM_F(gravityScale),
    VKM_F(isKinematic),
    VKM_F(isStatic)
VKM_REFLECT_END()

} // namespace Engine
