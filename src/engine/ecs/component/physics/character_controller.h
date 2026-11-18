#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Turns a desired direction into velocity on a Rigidbody, and reports what the body stands on.
 *
 * Driven by a System rather than a behavior: the writer of moveInput changes, the reader should not.
 * Needs a Rigidbody and a capsule Collider; the body should set freezeRotation, and canSleep false so
 * that standing still its contacts go on reporting and what rests on it stays awake. It climbs what
 * gameplay says it is on - a ladder, a rope, a wall - by `climbing`, crouches by `crouching`, and rides
 * what it stands on. Grounded, its steering follows the ground plane, down a slope as well as up it,
 * and speed off the ground - a landing's bounce, a crest's throw - is taken back.
 */
struct CharacterController {
    glm::vec3 moveInput     = {0.0f, 0.0f, 0.0f};  ///< Desired horizontal velocity, world space, m/s.
    bool      jumpRequested = false;               ///< Cleared each tick this end simulates, honored or not.

    float jumpSpeed     = 5.0f;   ///< Upward speed a jump starts at, m/s; asked while grounded.
    float acceleration  = 40.0f;  ///< How fast velocity closes on moveInput, m/s^2.
    float airControl    = 0.25f;  ///< Fraction of acceleration available while airborne.
    float maxSlopeAngle = 50.0f;  ///< Degrees; a steeper surface holds nothing up.

    float knockbackDrag = 0.0f;  ///< How fast a shove's horizontal speed bleeds off, m/s^2; zero leaves it to
                                 ///< steering. A shove is speed nobody asked for - a blast, a dash - past
                                 ///< both moveInput's and what steering left the body at (`walkSpeed`), so
                                 ///< letting go of the stick is not one. Above zero it is carried, losing
                                 ///< this much a second, while the character steers at `airControl` of the
                                 ///< rate, as if airborne.

    float stepHeight = 0.4f;  ///< Tallest obstacle, in meters, the character mounts instead of stopping at;
                              ///< zero disables it, and taller is a wall to slide along. A step needs clear
                              ///< space at the top and walkable ground beyond, checked before anything moves,
                              ///< so a tall setting climbs more, never through. It also caps how far ground
                              ///< falling away under a walking character in a tick may drop and still hold it
                              ///< (`grounded`); at zero, a crest or a dip can lift it off.

    bool      climbing    = false;               ///< Held to a face being climbed instead of by gravity, for
                                                 ///< the tick it is set. Gravity is undone; moveInput into
                                                 ///< the face climbs at up to `climbSpeed`, away from it
                                                 ///< climbs down, along it slides at half that. A jump leaps
                                                 ///< off, up at `jumpSpeed` and out at half of it, so
                                                 ///< gameplay lets go as it asks for one, or holds the leaper
                                                 ///< again next tick. Cleared after each tick: gameplay sets
                                                 ///< it, from where the body is, each fixed tick it holds on,
                                                 ///< and a character whose hold has gone falls.
    glm::vec3 climbNormal = {0.0f, 0.0f, 1.0f};  ///< Out of the face climbed, world space; level part counts.
    float     climbSpeed  = 3.0f;                ///< Up or down a face, m/s.

    bool  crouching    = false;  ///< Crouch while set: the capsule shortens to `crouchHeight`, its feet where
                                 ///< they were. Held, not cleared: gameplay sets it while the key is down.
                                 ///< Let go, the character stands once there is room over its head, and until
                                 ///< then stays `crouched`.
    float crouchHeight = 1.0f;   ///< The capsule's whole height crouched, caps included, meters.

    bool  grounded  = false;  ///< Read-only: standing on a surface within maxSlopeAngle. Held through a tick
                              ///< whose contact is lost while walkable ground lies within twice what a slope
                              ///< that steep drops in a tick at its speed, or a few centimeters, capped by
                              ///< `stepHeight`: it is pulled down onto it rather than flying off a crest or
                              ///< skipping down a slope.
    float walkSpeed = 0.0f;   ///< Read-only: horizontal speed its steering left, m/s.

    bool jumping = false;  ///< Read-only: thrown off the ground - a jump, a leap off a climb, a pad, a blast
                           ///< - and still rising off it; never grounded meanwhile, so the contact the tick
                           ///< after a jump reports does not take it back. Ground it stood on throws it by
                           ///< being left faster than a contact starts to bounce; a landing's bounce never
                           ///< does, and grounding takes it back.

    // A climb outlives the condition that started it: it leaves the ground first, so re-deciding it
    // from `grounded` each tick would abort it. These carry it to the measured height.
    bool      stepping     = false;               ///< Read-only: mounting something this tick.
    float     stepTargetY  = 0.0f;                ///< Read-only: world Y the feet are climbing to.
    float     stepTime     = 0.0f;                ///< Read-only: seconds the current climb has run.
    glm::vec3 groundNormal = {0.0f, 1.0f, 0.0f};  ///< Read-only: its normal; world up when airborne.

    glm::vec3 groundVelocity = {0.0f, 0.0f, 0.0f};  ///< Read-only: how fast what it stands on carries its
                                                    ///< feet, m/s, which steering is relative to: a moving or
                                                    ///< turning body's, a kinematic one's by the velocities
                                                    ///< it is driven with, as a contact reads them. Zero on
                                                    ///< static ground, or in the air.
    float     groundSpin     = 0.0f;                ///< Read-only: how fast that turns about up, rad/s, for a
                                                    ///< view.

    bool  crouched           = false;  ///< Read-only: the capsule is short, by `crouching` or a low ceiling.
                                       ///< In a networked game: docs/reference/networking.md, "Prediction
                                       ///< and correction".
    float standingHalfHeight = 0.0f;   ///< Read-only: the capsule's halfHeight standing, kept while crouched.
};

} // namespace Vkm::Engine

// Only the tuning is serialized; the rest is per-tick traffic.
VKM_REFLECT_BEGIN(::Vkm::Engine::CharacterController)
    VKM_F(jumpSpeed, 0.0f, 100.0f, "Upward speed a jump starts at")
    VKM_F(acceleration, 0.0f, 1000.0f, "How fast its velocity closes on the direction asked for")
    VKM_F(airControl, 0.0f, 1.0f, "The share of that acceleration it has in the air")
    VKM_F(maxSlopeAngle, 0.0f, 90.0f, "Steeper than this holds nothing up: it slides")
    VKM_F(knockbackDrag, 0.0f, 1000.0f, "How fast a shove's speed bleeds off; 0 leaves it to steering")
    VKM_F(stepHeight, 0.0f, 10.0f, "Tallest thing it climbs instead of stopping; 0 makes a curb a wall")
    VKM_F(climbSpeed, 0.0f, 100.0f, "Up or down a face while gameplay holds it to one")
    VKM_F(crouchHeight, 0.1f, 10.0f, "The capsule's whole height crouched; its feet stay put")
VKM_REFLECT_END()
