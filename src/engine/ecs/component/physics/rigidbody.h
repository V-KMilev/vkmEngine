#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief How the solver moves a body.
 *
 * Serialized by name, so reordering keeps scenes valid.
 */
enum class RigidbodyMotion : uint8_t {
    Dynamic   = 0,  ///< Forces and contacts move it, by its mass
    Kinematic = 1,  ///< Its Transform moves it; infinite mass to whatever it touches
    Static    = 2,  ///< Never moves; infinite mass
    Count           ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Dynamics state for a physics body: motion, material response and mass.
 *
 * PhysicsSystem integrates it each fixed tick and writes the pose back to the Transform. Derived mass
 * properties are re-derived each tick from mass and Collider into a BodyFrame, so edits need no "apply".
 * A parented body's pose is resolved by walking its Transform chain, and writeback converts the solved
 * pose back into the parent's frame.
 */
struct Rigidbody {
    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};  ///< World-space velocity (m/s)
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};  ///< World-space spin (rad/s, axis * speed)

    /**
     * @brief How the solver moves it.
     *
     * Kinematic ignores forces and impulses; its velocities are not integrated but still reach a contact -
     * a platform pushes what stands on it - so a body that stops being driven should zero them. A bone an
     * inactive Ragdoll poses moves as Kinematic whatever this says (isPosedByAnimation).
     */
    RigidbodyMotion motion = RigidbodyMotion::Dynamic;

    /// Kilograms, read only when Dynamic. Must be positive: PhysicsSystem reports one that is not and holds
    /// it still.
    float mass = 1.0f;

    float linearDamping  = 0.01f;  ///< Per-second velocity bleed (drag)
    float angularDamping = 0.05f;  ///< Per-second spin bleed

    float restitution = 0.2f;  ///< Bounciness [0,1]
    float friction    = 0.5f;  ///< Coulomb coefficient [0,1+]

    float gravityScale = 1.0f;  ///< Multiplier on world gravity (0 = floats)

    /**
     * @brief Which group this body belongs to, as a single bit; paired with `collidesWith`.
     *
     * 0 collides with nothing, which switches collision off without disabling the collider that draws it.
     */
    int layer = 1;

    /**
     * @brief Which layers this body collides with, as a mask of their bits.
     *
     * A pair collides only if each body's layer is in the other's mask.
     */
    int collidesWith = ~0;

    bool freezeRotation = false;  ///< Contacts never torque the body (character controllers)
    bool canSleep       = true;   ///< False keeps a script-driven body responsive
    bool sleeping       = false;  ///< Rested long enough; skipped until disturbed

    float sleepTimer = 0.0f;  ///< Runtime-only: seconds spent resting

    /**
     * @brief Whether a resolved contact reached this body last tick, and the most upward normal among them.
     *
     * Written by PhysicsSystem::writeback, never read by it; triggers never count. Most upward is the
     * largest +Y component. The normal is as the surface acts on THIS body, so a contact's two bodies see
     * opposite normals.
     */
    glm::vec3 supportNormal = {0.0f, 1.0f, 0.0f};
    bool      supported     = false;

    /**
     * @brief The most horizontal normal among the same contacts: what blocks this body.
     *
     * Valid only while supported. A body touching only level floor leaves it at world up.
     */
    glm::vec3 blockNormal = {0.0f, 1.0f, 0.0f};

    /**
     * @brief Wake a sleeping body so the next tick moves it.
     *
     * Clears the timer too: a cleared flag with a long rest on the timer falls asleep again that tick.
     *
     * @param rb Body to wake.
     */
    static void wake(Rigidbody& rb) {
        rb.sleeping   = false;
        rb.sleepTimer = 0.0f;
    }
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::RigidbodyMotion, "Dynamic", "Kinematic", "Static")

// sleeping / sleepTimer / supported / supportNormal / blockNormal are runtime-only.
VKM_REFLECT_BEGIN(::Vkm::Engine::Rigidbody)
    VKM_F(linearVelocity)
    VKM_F(angularVelocity)
    VKM_F(motion)
    VKM_F(mass)
    VKM_F(linearDamping)
    VKM_F(angularDamping)
    VKM_F(restitution)
    VKM_F(friction)
    VKM_F(gravityScale)
    VKM_F(layer)
    VKM_F(collidesWith)
    VKM_F(freezeRotation)
    VKM_F(canSleep)
VKM_REFLECT_END()
