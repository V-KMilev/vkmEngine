#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Turns a desired direction into velocity on a Rigidbody, and reports what the body stands on.
 *
 * Driven by a System rather than a Behavior: the writer of moveInput changes, the reader should not.
 * Needs a Rigidbody and a capsule Collider; the body should set freezeRotation and canSleep false, so
 * standing still does not park a body the input then has to wake. No crouch, no platform riding.
 */
struct CharacterController {
    glm::vec3 moveInput     = {0.0f, 0.0f, 0.0f};  ///< Desired horizontal velocity, world space, m/s.
    bool      jumpRequested = false;               ///< Cleared each tick this end simulates, honoured or not.

    float jumpSpeed     = 5.0f;    ///< Upward speed a jump starts at, m/s.
    float acceleration  = 40.0f;   ///< How fast velocity closes on moveInput, m/s^2.
    float airControl    = 0.25f;   ///< Fraction of acceleration available while airborne.
    float maxSlopeAngle = 50.0f;   ///< Degrees; a steeper surface holds nothing up.

    /**
     * @brief Tallest obstacle, in metres, the character mounts instead of stopping at; zero disables it.
     *
     * Taller is a wall to slide along. A step needs clear space at the top and walkable ground beyond,
     * checked before anything moves, so a tall setting climbs more, never through.
     */
    float stepHeight = 0.4f;

    bool grounded = false;                       ///< Read-only: on a surface within maxSlopeAngle.

    // A climb outlives the condition that started it: it leaves the ground first, so re-deciding it
    // from `grounded` each tick would abort it. These carry it to the measured height.
    bool      stepping     = false;               ///< Read-only: mounting something this tick.
    float     stepTargetY  = 0.0f;                ///< Read-only: world Y the feet are climbing to.
    float     stepTime     = 0.0f;                ///< Read-only: seconds the current climb has run.
    glm::vec3 groundNormal = {0.0f, 1.0f, 0.0f};  ///< Read-only: its normal; world up when airborne.
};

} // namespace Vkm::Engine

// Only the tuning is serialized; the rest is per-tick traffic.
VKM_REFLECT_BEGIN(::Vkm::Engine::CharacterController)
    VKM_F(jumpSpeed)
    VKM_F(acceleration)
    VKM_F(airControl)
    VKM_F(maxSlopeAngle)
    VKM_F(stepHeight)
VKM_REFLECT_END()
