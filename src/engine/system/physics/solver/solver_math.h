#pragma once

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
inline void applyImpulse(PhysicsBody& body, const glm::vec3& impulse,
                         const glm::vec3& r, float sign) {
    body.linearVelocity  += impulse * (body.invMass * sign);
    body.angularVelocity += body.invInertiaWorld * glm::cross(r, impulse * sign);
}

/**
 * @brief The mass a pair presents along one direction at one pair of lever arms.
 *
 * What an impulse there actually moves, once both bodies' inertia is counted.
 * Zero when both bodies are immovable: nothing to solve, and the caller's
 * division would otherwise be one by zero rather than an infinite mass.
 *
 * @param a First body.
 * @param b Second body.
 * @param rA Lever arm on @p a.
 * @param rB Lever arm on @p b.
 * @param direction Unit direction the mass is asked along.
 * @return 1 / k, or zero when k vanishes.
 */
inline float effectiveMass(const PhysicsBody& a, const PhysicsBody& b,
                           const glm::vec3& rA, const glm::vec3& rB,
                           const glm::vec3& direction) {
    const glm::vec3 crossA = glm::cross(rA, direction);
    const glm::vec3 crossB = glm::cross(rB, direction);
    const float k = a.invMass + b.invMass
                  + glm::dot(crossA, a.invInertiaWorld * crossA)
                  + glm::dot(crossB, b.invInertiaWorld * crossB);
    return k > glm::epsilon<float>() ? 1.0f / k : 0.0f;
}

} // namespace Vkm::Engine::SolverMath
