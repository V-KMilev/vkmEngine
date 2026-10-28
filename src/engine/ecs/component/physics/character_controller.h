#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Turns a desired direction into velocity on a Rigidbody, and reports what the body stands on.
 *
 * Driven by a System rather than a Behavior: the writer of moveInput changes, the reader should not.
 * Needs a Rigidbody and a capsule Collider; the body should set freezeRotation, and canSleep false so
 * that standing still its contacts go on reporting and what rests on it stays awake. It climbs what
 * gameplay says it is on - a ladder, a rope, a wall - by `climbing`, crouches by `crouching`, and rides
 * what it stands on.
 */
struct CharacterController {
    glm::vec3 moveInput     = {0.0f, 0.0f, 0.0f};  ///< Desired horizontal velocity, world space, m/s.
    bool      jumpRequested = false;               ///< Cleared each tick this end simulates, honoured or not.

    float jumpSpeed     = 5.0f;    ///< Upward speed a jump starts at, m/s.
    float acceleration  = 40.0f;   ///< How fast velocity closes on moveInput, m/s^2.
    float airControl    = 0.25f;   ///< Fraction of acceleration available while airborne.
    float maxSlopeAngle = 50.0f;   ///< Degrees; a steeper surface holds nothing up.

    /**
     * @brief How fast a shove's horizontal speed bleeds off, m/s^2; zero leaves it to steering.
     *
     * A shove is speed nobody asked for - a blast, a dash: horizontal speed past both moveInput's and
     * what the controller's own steering left the body at (`walkSpeed`), so letting go of the stick is
     * not one. Above zero it is carried, losing this much a second, and the character steers at
     * `airControl` of the rate meanwhile, as if airborne.
     */
    float knockbackDrag = 0.0f;

    /**
     * @brief Tallest obstacle, in metres, the character mounts instead of stopping at; zero disables it.
     *
     * Taller is a wall to slide along. A step needs clear space at the top and walkable ground beyond,
     * checked before anything moves, so a tall setting climbs more, never through. It also caps how far
     * ground falling away under a walking character in one tick may drop and still hold it (`grounded`);
     * at zero, a crest or a dip in a slope can lift it off.
     */
    float stepHeight = 0.4f;

    /**
     * @brief Held to a face being climbed instead of by gravity, for the tick it is set.
     *
     * Gravity is undone; moveInput into the face climbs at up to `climbSpeed`, away from it climbs
     * down, along it slides at half its speed. A jump leaps off, up at `jumpSpeed` and out at half
     * of it - so gameplay lets go as it asks for one, or holds the leaper again the next tick. The
     * controller clears it after each tick, so gameplay sets it - from where the body is - each
     * fixed tick it holds on, and a character whose hold has gone falls.
     */
    bool      climbing    = false;
    glm::vec3 climbNormal = {0.0f, 0.0f, 1.0f};  ///< Out of the face climbed, world space; level part counts.
    float     climbSpeed  = 3.0f;                ///< Up or down a face, m/s.

    /**
     * @brief Crouch while set: the capsule shortens to `crouchHeight`, its feet where they were.
     *
     * Held, not cleared: gameplay sets it while the key is down. Let go, the character stands once
     * there is room over its head, and until then stays `crouched`.
     */
    bool  crouching    = false;
    float crouchHeight = 1.0f;  ///< The capsule's whole height crouched, caps included, metres.

    /**
     * @brief Read-only: standing on a surface within maxSlopeAngle.
     *
     * Held through a tick whose contact is lost while walkable ground lies within twice what a slope
     * that steep drops in a tick at its speed, or a few centimetres, capped by `stepHeight`: it is
     * pulled down onto it rather than left to fly off a crest or skip down a slope.
     */
    bool  grounded  = false;
    float walkSpeed = 0.0f;  ///< Read-only: horizontal speed its steering left, m/s.

    /**
     * @brief Read-only: thrown off the ground - by a jump, a leap off a climb, a pad or a blast - and still
     *        rising off it.
     *
     * Never grounded meanwhile, so the contact the tick after a jump still reports does not take the
     * jump back. Ground it stood on throws it by being left faster than a contact starts to bounce;
     * a landing's bounce never does, and grounding takes it back.
     */
    bool jumping = false;

    // A climb outlives the condition that started it: it leaves the ground first, so re-deciding it
    // from `grounded` each tick would abort it. These carry it to the measured height.
    bool      stepping     = false;               ///< Read-only: mounting something this tick.
    float     stepTargetY  = 0.0f;                ///< Read-only: world Y the feet are climbing to.
    float     stepTime     = 0.0f;                ///< Read-only: seconds the current climb has run.
    glm::vec3 groundNormal = {0.0f, 1.0f, 0.0f};  ///< Read-only: its normal; world up when airborne.

    /**
     * @brief Read-only: how fast what it stands on carries its feet, m/s, which steering is relative to.
     *
     * A moving or turning body - a kinematic one by the velocities it is driven with, as a contact
     * reads them. Zero on static ground, or in the air.
     */
    glm::vec3 groundVelocity = {0.0f, 0.0f, 0.0f};
    float     groundSpin     = 0.0f;  ///< Read-only: how fast that turns about up, rad/s, for a view.

    bool  crouched           = false;  ///< Read-only: the capsule is short, by `crouching` or a low ceiling.
    float standingHalfHeight = 0.0f;   ///< Read-only: the capsule's halfHeight standing, kept while crouched.
};

} // namespace Vkm::Engine

// Only the tuning is serialized; the rest is per-tick traffic.
VKM_REFLECT_BEGIN(::Vkm::Engine::CharacterController)
    VKM_F(jumpSpeed)
    VKM_F(acceleration)
    VKM_F(airControl)
    VKM_F(maxSlopeAngle)
    VKM_F(knockbackDrag)
    VKM_F(stepHeight)
    VKM_F(climbSpeed)
    VKM_F(crouchHeight)
VKM_REFLECT_END()
