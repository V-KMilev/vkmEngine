#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Turns a desired direction into velocity on a Rigidbody, and reports
 *        what the body is standing on.
 *
 * A component driven by a System rather than a Behavior, because the thing that
 * writes moveInput changes and the thing that reads it should not: gameplay
 * writes it today and a nav agent writes it later, and an engine system cannot
 * address a hot-reloadable game Behavior. Whoever writes moveInput needs to know
 * nothing about physics, and CharacterControllerSystem needs to know nothing
 * about who wrote it.
 *
 * The entity needs a Rigidbody and a capsule Collider. The body should also set
 * freezeRotation - a character that tips over on the first wall it touches is a
 * character, physically speaking, but not one anybody wants - and canSleep
 * false, so standing still does not park the body the input then has to wake.
 *
 * Deliberately partial for 1.6: velocity-driven, with no step-up (that needs a
 * shapecast query the engine does not have) and no crouch or platform riding.
 */
struct CharacterController {
    glm::vec3 moveInput = {0.0f, 0.0f, 0.0f};   ///< Desired horizontal velocity, world space, m/s.
    bool jumpRequested  = false;                ///< Consumed and cleared every tick, honoured or not.

    float jumpSpeed     = 5.0f;    ///< Upward speed a jump starts at, m/s.
    float acceleration  = 40.0f;   ///< How fast velocity closes on moveInput, m/s^2.
    float airControl    = 0.25f;   ///< Fraction of acceleration available while airborne.
    float maxSlopeAngle = 50.0f;   ///< Degrees; a steeper surface holds nothing up.

    bool grounded = false;                       ///< Read-only: on a surface within maxSlopeAngle.
    glm::vec3 groundNormal = {0.0f, 1.0f, 0.0f}; ///< Read-only: its normal; world up when airborne.
};

} // namespace Vkm::Engine

// moveInput / jumpRequested / grounded / groundNormal are per-tick traffic, not
// authored state, and are intentionally absent: only the tuning is serialized.
VKM_REFLECT_BEGIN(::Vkm::Engine::CharacterController)
    VKM_F(jumpSpeed),
    VKM_F(acceleration),
    VKM_F(airControl),
    VKM_F(maxSlopeAngle)
VKM_REFLECT_END()
