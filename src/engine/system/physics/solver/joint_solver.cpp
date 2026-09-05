#include "system/physics/solver/joint_solver.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "system/physics/solver/solver_math.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

using SolverMath::applyImpulse;
using SolverMath::effectiveMass;
using SolverMath::velocityAt;

// r as a matrix, so that cross(r, x) is a multiplication and the constraint's
// mass can be assembled rather than sampled.
glm::mat3 skew(const glm::vec3& r) {
    return glm::mat3( 0.0f,  r.z, -r.y,
                     -r.z,  0.0f,  r.x,
                      r.y, -r.x,  0.0f);
}

// What an impulse at these lever arms moves, in every direction at once.
//
// A point joint removes three degrees of freedom, and with any rotational
// inertia the mass differs per direction - so a scalar taken along one axis and
// used for a correction along another overshoots, and an overshoot inside an
// iteration loop is not an error that decays but one that grows.
glm::mat3 effectiveMassMatrix(const PhysicsBody& a, const PhysicsBody& b,
                              const glm::vec3& rA, const glm::vec3& rB) {
    const glm::mat3 skewA = skew(rA);
    const glm::mat3 skewB = skew(rB);
    glm::mat3 k(a.invMass + b.invMass);
    k -= skewA * a.invInertiaWorld * skewA;
    k -= skewB * b.invInertiaWorld * skewB;
    return k;
}

/**
 * @brief Fraction of a joint's position error corrected per tick.
 *
 * The same number the contact solver's soft step corrects a contact by: a joint
 * correcting at its own private rate would fight the contacts for the same
 * bodies.
 */
constexpr float JOINT_BAUMGARTE = 0.2f;

} // namespace

void solveJoints(
    std::vector<PhysicsBody>& bodies,
    const std::vector<JointConstraint>& joints,
    const SolverParams& params
) {
    // Divided by the timestep because it buys a velocity and the velocity has
    // a tick to act in.
    const float rate = params.dt > glm::epsilon<float>()
                     ? JOINT_BAUMGARTE / params.dt
                     : 0.0f;

    for (int pass = 0; pass < params.iterations; ++pass) {
        for (const JointConstraint& joint : joints) {
            const size_t count = bodies.size();
            if (joint.bodyA >= count || joint.bodyB >= count) continue;

            PhysicsBody& a = bodies[joint.bodyA];
            PhysicsBody& b = bodies[joint.bodyB];
            if (a.invMass + b.invMass <= 0.0f) continue;

            const glm::vec3 worldA = a.position + joint.anchorA;
            const glm::vec3 worldB = b.position + joint.anchorB;
            const glm::vec3 separation = worldB - worldA;
            const glm::vec3 relative = velocityAt(b, joint.anchorB)
                                     - velocityAt(a, joint.anchorA);

            if (joint.distance < 0.0f) {
                // A point joint: every direction is constrained, so the error
                // is the whole separation and the correction is a vector.
                const glm::mat3 k =
                    effectiveMassMatrix(a, b, joint.anchorA, joint.anchorB);
                // Judged against the matrix's own scale: the determinant goes as
                // inverse mass cubed, so a fixed epsilon calls every joint between
                // heavy bodies singular. Relative, mass drops out.
                const float scale = (k[0][0] + k[1][1] + k[2][2]) / 3.0f;
                const float measure = scale * scale * scale;
                if (measure <= 0.0f
                    || std::fabs(glm::determinant(k))
                           <= glm::epsilon<float>() * measure) {
                    continue;
                }

                // The two terms add: the impulse has to cancel the relative
                // motion and close the gap. Stiffness scales the bias, not each
                // pass's impulse, which would compound over the iterations.
                const glm::vec3 bias = separation * (rate * joint.stiffness);
                const glm::vec3 impulse = glm::inverse(k) * (-(relative + bias));

                applyImpulse(a, impulse, joint.anchorA, -1.0f);
                applyImpulse(b, impulse, joint.anchorB, +1.0f);
                continue;
            }

            // A distance joint: only the component along the line between the
            // anchors is constrained, and everything across it is free.
            const float length = glm::length(separation);
            if (length <= Physics::CONTACT_TOLERANCE) continue;

            const glm::vec3 axis = separation / length;
            const float mass =
                effectiveMass(a, b, joint.anchorA, joint.anchorB, axis);
            if (mass <= 0.0f) continue;

            const float error = length - joint.distance;
            const float bias = error * (rate * joint.stiffness);
            const float along = glm::dot(relative, axis);
            const float magnitude = -(along + bias) * mass;

            const glm::vec3 impulse = axis * magnitude;
            applyImpulse(a, impulse, joint.anchorA, -1.0f);
            applyImpulse(b, impulse, joint.anchorB, +1.0f);
        }
    }
}

} // namespace Vkm::Engine
