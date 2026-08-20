#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Dynamics state for a physics body: linear + angular motion, material
 *        response, and mass properties.
 *
 * PhysicsSystem integrates this each fixed tick and writes the resulting pose
 * back to the entity's Transform. A static or kinematic body has infinite mass:
 * forces never move it, but it still acts as an immovable wall during collision.
 *
 * The derived mass properties (inverse mass, body-local inverse inertia tensor)
 * are not stored here - PhysicsSystem re-derives them from mass + Collider into
 * its own per-tick BodyFrame, so editing either takes effect without a separate
 * "apply" step.
 *
 * A hierarchy root's local Transform is already its world pose, so the solver reads
 * it directly. A parented body takes its world pose from WorldTransform instead, and
 * writeback converts the solved pose back into the parent's frame.
 */
struct Rigidbody {
    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};  ///< World-space velocity (m/s)
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};  ///< World-space spin (rad/s, axis * speed)

    float mass = 1.0f;                               ///< Authoring mass in kg; <= 0 is treated as static

    float linearDamping  = 0.01f;                    ///< Per-second velocity bleed (drag)
    float angularDamping = 0.05f;                    ///< Per-second spin bleed

    float restitution = 0.2f;                        ///< Bounciness [0,1]
    float friction    = 0.5f;                        ///< Coulomb coefficient [0,1+]

    float gravityScale = 1.0f;                        ///< Multiplier on world gravity (0 = floats)

    bool isKinematic = false;                         ///< Script-driven: ignores forces, immune to impulses, still moves
    bool isStatic    = false;                         ///< Never moves; infinite mass
    bool freezeRotation = false;                      ///< Dynamic translation only: contacts never torque the body (character controllers)
    bool canSleep    = true;                          ///< Opt out of sleeping (false) for script-driven bodies that must stay responsive
    bool sleeping    = false;                         ///< Below energy threshold; skipped until disturbed

    float sleepTimer = 0.0f;                          ///< Runtime-only: seconds spent resting; not persisted

    /**
     * @brief Whether a resolved contact held this body up on the last tick, and
     *        the most upward normal among those contacts.
     *
     * Written by PhysicsSystem::writeback, never read by it. Generic outputs,
     * not a character feature: "am I standing on something, and how steep is
     * it" is what a controller, a footstep sound and a landing animation all
     * ask, and putting the answer here is what keeps PhysicsSystem from knowing
     * that any of them exist. A trigger holds nothing up, so it never counts.
     *
     * Most upward means the largest +Y component, matching the engine's world
     * up - the same axis a capsule collider stands along. supportNormal is the
     * surface's normal as it acts on THIS body, so the two bodies of one
     * contact see opposite normals.
     */
    glm::vec3 supportNormal = {0.0f, 1.0f, 0.0f};
    bool      supported     = false;
};

// sleeping / sleepTimer / supported / supportNormal are runtime-only, and
// intentionally absent.
} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Rigidbody)
    VKM_F(linearVelocity),
    VKM_F(angularVelocity),
    VKM_F(mass),
    VKM_F(linearDamping),
    VKM_F(angularDamping),
    VKM_F(restitution),
    VKM_F(friction),
    VKM_F(gravityScale),
    VKM_F(isKinematic),
    VKM_F(isStatic),
    VKM_F(freezeRotation),
    VKM_F(canSleep)
VKM_REFLECT_END()
