#include "system/physics/solver/solver.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "system/physics/solver/solver_math.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Combined material response for a contacting pair.
 *
 * Restitution takes the max, so a bouncy ball off a dead floor still bounces.
 * Friction is the geometric mean, the usual Coulomb pairing.
 */
float combineRestitution(const PhysicsBody& a, const PhysicsBody& b) {
    return std::max(a.restitution, b.restitution);
}

float combineFriction(const PhysicsBody& a, const PhysicsBody& b) {
    return std::sqrt(std::max(0.0f, a.friction * b.friction));
}

// The contact and joint solvers share their velocity/impulse arithmetic
// through SolverMath; the pseudo pair stays here because split-impulse
// position correction is this solver's own device.
using SolverMath::applyImpulse;
using SolverMath::effectiveMass;
using SolverMath::velocityAt;

glm::vec3 pseudoVelocityAt(const PhysicsBody& body, const glm::vec3& r) {
    return body.pseudoLinear + glm::cross(body.pseudoAngular, r);
}

void applyPseudoImpulse(PhysicsBody& body, const glm::vec3& impulse, const glm::vec3& r, float sign) {
    body.pseudoLinear  += sign * body.invMass * impulse;
    body.pseudoAngular += sign * body.invInertiaWorld * glm::cross(r, impulse);
}

} // namespace

void solveContacts(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    const SolverParams& params
) {
    // Taken from the pre-solve approach speed, once, so that every pass aims at
    // the same separation speed. Recomputing per iteration cancels the bounce.
    for (ContactManifold& manifold : manifolds) {
        PhysicsBody& a = bodies[manifold.bodyA];
        PhysicsBody& b = bodies[manifold.bodyB];
        const float restitution = combineRestitution(a, b);
        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            contact.rA = contact.point - a.position;
            contact.rB = contact.point - b.position;
            contact.normalMass = effectiveMass(a, b, contact.rA, contact.rB, contact.normal);

            const glm::vec3 relVel = velocityAt(b, contact.rB) - velocityAt(a, contact.rA);
            const float vn = glm::dot(relVel, contact.normal);
            contact.restitutionBias =
                (-vn > params.restitutionThreshold) ? -restitution * vn : 0.0f;
        }
    }

    // Velocity resolution: normal impulses (toward the restitution target) plus
    // clamped Coulomb friction, accumulated across iterations so they converge.
    for (int iter = 0; iter < params.iterations; ++iter) {
        for (ContactManifold& manifold : manifolds) {
            PhysicsBody& a = bodies[manifold.bodyA];
            PhysicsBody& b = bodies[manifold.bodyB];
            const float friction = combineFriction(a, b);

            for (int c = 0; c < manifold.count; ++c) {
                Contact& contact = manifold.contacts[c];
                const glm::vec3& rA = contact.rA;
                const glm::vec3& rB = contact.rB;

                const glm::vec3 relVel = velocityAt(b, rB) - velocityAt(a, rA);
                const float vn = glm::dot(relVel, contact.normal);

                float jn = -contact.normalMass * (vn - contact.restitutionBias);

                // Clamp the *accumulated* normal impulse to be non-negative;
                // apply only the delta needed to reach the new total.
                const float oldNormal = contact.normalImpulse;
                contact.normalImpulse = std::max(0.0f, oldNormal + jn);
                jn = contact.normalImpulse - oldNormal;

                const glm::vec3 normalImpulse = jn * contact.normal;
                applyImpulse(a, normalImpulse, rA, -1.0f);
                applyImpulse(b, normalImpulse, rB, +1.0f);

                // Friction along the tangent of the post-normal relative motion.
                const glm::vec3 relVelT = velocityAt(b, rB) - velocityAt(a, rA);
                glm::vec3 tangent = relVelT - glm::dot(relVelT, contact.normal) * contact.normal;
                const float tLen = glm::length(tangent);
                if (tLen < Physics::FRICTION_DEADZONE) continue;
                tangent /= tLen;

                const float kt = effectiveMass(a, b, rA, rB, tangent);
                float jt = -kt * glm::dot(relVelT, tangent);

                const float maxFriction = friction * contact.normalImpulse;
                const float oldTangent = contact.tangentImpulse;
                contact.tangentImpulse = std::clamp(oldTangent + jt, -maxFriction, maxFriction);
                jt = contact.tangentImpulse - oldTangent;

                const glm::vec3 frictionImpulse = jt * tangent;
                applyImpulse(a, frictionImpulse, rA, -1.0f);
                applyImpulse(b, frictionImpulse, rB, +1.0f);
            }
        }
    }

    // Split-impulse position correction: a separate constraint solved with
    // pseudo-velocities so removing penetration does not add real kinetic energy.
    const float invDt = params.dt > 0.0f ? 1.0f / params.dt : 0.0f;
    for (int iter = 0; iter < params.iterations; ++iter) {
        for (ContactManifold& manifold : manifolds) {
            PhysicsBody& a = bodies[manifold.bodyA];
            PhysicsBody& b = bodies[manifold.bodyB];

            for (int c = 0; c < manifold.count; ++c) {
                Contact& contact = manifold.contacts[c];
                const float depth = contact.penetration - params.penetrationSlop;
                if (depth <= 0.0f) continue;

                const glm::vec3& rA = contact.rA;
                const glm::vec3& rB = contact.rB;

                const glm::vec3 relVel = pseudoVelocityAt(b, rB) - pseudoVelocityAt(a, rA);
                const float vn = glm::dot(relVel, contact.normal);
                const float bias = params.baumgarte * depth * invDt;

                const float jp = contact.normalMass * (bias - vn);
                if (jp <= 0.0f) continue;

                const glm::vec3 impulse = jp * contact.normal;
                applyPseudoImpulse(a, impulse, rA, -1.0f);
                applyPseudoImpulse(b, impulse, rB, +1.0f);
            }
        }
    }
}

} // namespace Vkm::Engine
