#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "system/physics/solver/solver.h"

namespace Vkm::Engine::SolverMath {

/**
 * @brief Velocity of the point at lever arm @p r on @p body.
 *
 * @param body Body the point rides on.
 * @param r Lever arm from the body's origin to the point, world space.
 * @return Linear velocity of that point.
 */
inline glm::vec3 velocityAt(const PhysicsBody& body, const glm::vec3& r) {
    return body.linearVelocity + glm::cross(body.angularVelocity, r);
}

/**
 * @brief Apply @p impulse at lever arm @p r, signed.
 *
 * @param body Body to push.
 * @param impulse Impulse vector, world space.
 * @param r Lever arm from the body's origin to the application point.
 * @param sign +1 to apply, -1 for the equal-and-opposite half of a pair.
 */
inline void applyImpulse(PhysicsBody& body, const glm::vec3& impulse, const glm::vec3& r, float sign) {
    body.linearVelocity  += impulse * (body.invMass * sign);
    body.angularVelocity += body.invInertiaWorld * glm::cross(r, impulse * sign);
}

/**
 * @brief The mass a pair presents along one direction at one pair of lever arms.
 *
 * k is tested against zero, not an epsilon: a twenty-thousand-tonne body's inverse mass is below it.
 *
 * @param a First body.
 * @param b Second body.
 * @param rA Lever arm on @p a.
 * @param rB Lever arm on @p b.
 * @param direction Unit direction the mass is asked along.
 * @return 1 / k, or zero when both bodies are immovable.
 */
inline float effectiveMass(
    const PhysicsBody& a,
    const PhysicsBody& b,
    const glm::vec3& rA,
    const glm::vec3& rB,
    const glm::vec3& direction
) {
    const glm::vec3 crossA = glm::cross(rA, direction);
    const glm::vec3 crossB = glm::cross(rB, direction);
    const float k = a.invMass + b.invMass
        + glm::dot(crossA, a.invInertiaWorld * crossA)
        + glm::dot(crossB, b.invInertiaWorld * crossB);
    return k > 0.0f ? 1.0f / k : 0.0f;
}

/**
 * @brief The three numbers that make a correction a stiff, damped spring.
 *
 * Catto's soft-constraint coefficients. `biasRate` turns position error into closing velocity,
 * `massScale` softens its impulse, `impulseScale` leaks the accumulated impulse, so the correction
 * settles where rigid Baumgarte rings. The default is no spring, as an unbiased pass solves.
 */
struct SoftConstraint {
    float biasRate     = 0.0f;
    float massScale    = 1.0f;
    float impulseScale = 0.0f;
};

/**
 * @brief The spring coefficients for a frequency and a damping ratio at a step.
 *
 * @param hertz Spring frequency; clamped to half the tick rate.
 * @param damping Damping ratio; above 1 does not ring.
 * @param dt Fixed timestep, seconds.
 * @return The coefficients, or no spring for a step of no length.
 */
inline SoftConstraint softness(float hertz, float damping, float dt) {
    if (dt <= 0.0f) return {};
    // Half the tick rate is what the step can carry; without damping and relax passes it would ring.
    const float clamped = std::min(hertz, 0.5f / dt);
    const float omega   = 2.0f * glm::pi<float>() * clamped;
    const float a1      = 2.0f * damping + dt * omega;
    const float a2      = dt * omega * a1;
    const float a3      = 1.0f / (1.0f + a2);
    return SoftConstraint{omega / a1, a2 * a3, a3};
}

} // namespace Vkm::Engine::SolverMath
