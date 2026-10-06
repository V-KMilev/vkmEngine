#include "system/physics/solver/joint_solver.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

using SolverMath::applyImpulse;
using SolverMath::effectiveMass;
using SolverMath::SoftConstraint;
using SolverMath::softness;
using SolverMath::velocityAt;

/**
 * @brief How stiff a joint is, as a spring frequency in hertz.
 *
 * Asks for the stiffest spring the step allows: below a 120 Hz tick, softness() clamps it to half the
 * tick rate.
 */
constexpr float JOINT_HERTZ = 60.0f;

/**
 * @brief Damping ratio of that spring.
 *
 * At 60 Hz a joint closes about two fifths of its drift per tick against an overlap's eighth, so a limb
 * does not sag off, and contacts still get the last word in each pass.
 */
constexpr float JOINT_DAMPING = 2.0f;

/**
 * @brief The hold's spring, in hertz: a muscle's pull, well below a joint's own.
 *
 * Its torque cap is what makes it give; the spring sets how quickly a held pair turns back.
 */
constexpr float HOLD_HERTZ = 8.0f;

/// Critically damped, so a held limb settles rather than wobbles.
constexpr float HOLD_DAMPING = 1.0f;

// cross(r, x) as a matrix, so the constraint's mass can be assembled rather than sampled.
glm::mat3 skew(const glm::vec3& r) {
    return glm::mat3(0.0f, r.z, -r.y, -r.z, 0.0f, r.x, r.y, -r.x, 0.0f);
}

// What an impulse at these lever arms moves, in every direction at once. With rotational inertia the
// mass differs per direction, and a scalar used off its axis overshoots - an error that grows per pass.
glm::mat3 effectiveMassMatrix(
    const PhysicsBody& a,
    const PhysicsBody& b,
    const glm::vec3& rA,
    const glm::vec3& rB
) {
    const glm::mat3 skewA = skew(rA);
    const glm::mat3 skewB = skew(rB);
    glm::mat3 k(a.invMass + b.invMass);
    k -= skewA * a.invInertiaWorld * skewA;
    k -= skewB * b.invInertiaWorld * skewB;
    return k;
}

// The rotation that carries @p from onto @p to, as an axis scaled by its angle, the shorter way round.
glm::vec3 rotationBetween(const glm::quat& from, const glm::quat& to) {
    glm::quat delta = to * glm::conjugate(from);
    if (delta.w < 0.0f) delta = -delta;
    const glm::vec3 axis(delta.x, delta.y, delta.z);
    const float sine = glm::length(axis);
    if (sine <= glm::epsilon<float>()) return axis * 2.0f;
    return axis * (2.0f * std::atan2(sine, delta.w) / sine);
}

// Clamped to the hold's limit, so a load past it turns the joint rather than storing up.
glm::vec3 withinLimit(const glm::vec3& impulse, float limit) {
    const float length = glm::length(impulse);
    return length > limit ? impulse * (limit / length) : impulse;
}

void prepareHold(
    const PhysicsBody& a,
    const PhysicsBody& b,
    JointConstraint& joint,
    float biasRate,
    float dt
) {
    joint.holdSolvable = false;
    const glm::vec3 carried = joint.holdImpulse;
    joint.holdImpulse = glm::vec3(0.0f);
    joint.holdLimit = joint.holdTorque * dt;
    if (joint.holdLimit <= 0.0f) return;

    const glm::mat3 k = a.invInertiaWorld + b.invInertiaWorld;
    const float scale = (k[0][0] + k[1][1] + k[2][2]) / 3.0f;
    const float measure = scale * scale * scale;
    if (measure <= 0.0f || std::fabs(glm::determinant(k)) <= glm::epsilon<float>() * measure) return;

    // Error is how far B has turned past where the hold keeps it; the bias spins it back.
    const glm::quat target = a.rotation * joint.heldRotation;
    joint.holdMass     = glm::inverse(k);
    joint.holdBias     = rotationBetween(target, b.rotation) * biasRate;
    joint.holdImpulse  = withinLimit(carried, joint.holdLimit);
    joint.holdSolvable = true;
}

} // namespace

JointSprings prepareJoints(
    const std::vector<PhysicsBody>& bodies,
    std::vector<JointConstraint>& joints,
    float dt
) {
    const SoftConstraint soft = softness(JOINT_HERTZ, JOINT_DAMPING, dt);
    const SoftConstraint hold = softness(HOLD_HERTZ, HOLD_DAMPING, dt);

    for (JointConstraint& joint : joints) {
        joint.solvable = false;

        const size_t count = bodies.size();
        if (joint.bodyA >= count || joint.bodyB >= count) {
            joint.impulse     = glm::vec3(0.0f);
            joint.holdImpulse = glm::vec3(0.0f);
            joint.holdSolvable = false;
            continue;
        }

        // Neither end can move (a sleeping chain): skipped, its impulse kept until it wakes.
        const PhysicsBody& a = bodies[joint.bodyA];
        const PhysicsBody& b = bodies[joint.bodyB];
        joint.holdSolvable = false;
        if (a.invMass + b.invMass <= 0.0f) continue;

        prepareHold(a, b, joint, hold.biasRate, dt);

        const glm::vec3 carried = joint.impulse;
        joint.impulse = glm::vec3(0.0f);

        const glm::vec3 separation = (b.position + joint.anchorB) - (a.position + joint.anchorA);

        // Scales the drift closed, not each pass's impulse, which would compound over the iterations.
        const float rate = soft.biasRate * joint.stiffness;

        if (joint.distance < 0.0f) {
            const glm::mat3 k = effectiveMassMatrix(a, b, joint.anchorA, joint.anchorB);
            // Relative to the matrix's own scale: the determinant goes as inverse mass cubed, so a fixed
            // epsilon would call every joint between heavy bodies singular.
            const float scale = (k[0][0] + k[1][1] + k[2][2]) / 3.0f;
            const float measure = scale * scale * scale;
            if (measure <= 0.0f
                || std::fabs(glm::determinant(k)) <= glm::epsilon<float>() * measure) {
                continue;
            }
            joint.mass    = glm::inverse(k);
            joint.bias    = separation * rate;
            joint.impulse = carried;
        } else {
            const float length = glm::length(separation);
            if (length <= Physics::CONTACT_TOLERANCE) continue;

            const glm::vec3 axis = separation / length;
            const float axial = effectiveMass(a, b, joint.anchorA, joint.anchorB, axis);
            if (axial <= 0.0f) continue;

            joint.mass    = axial * glm::outerProduct(axis, axis);
            joint.bias    = axis * ((length - joint.distance) * rate);
            joint.impulse = axis * glm::dot(axis, carried);
        }
        joint.solvable = true;
    }
    return JointSprings{soft, hold};
}

void warmStartJoints(std::vector<PhysicsBody>& bodies, const std::vector<JointConstraint>& joints) {
    for (const JointConstraint& joint : joints) {
        if (!joint.solvable && !joint.holdSolvable) continue;

        // By reference: PhysicsSystem::gatherJoints never joins a body to itself.
        PhysicsBody& a = bodies[joint.bodyA];
        PhysicsBody& b = bodies[joint.bodyB];
        if (joint.solvable) {
            applyImpulse(a, joint.impulse, joint.anchorA, -1.0f);
            applyImpulse(b, joint.impulse, joint.anchorB, +1.0f);
        }
        if (joint.holdSolvable) {
            a.angularVelocity -= a.invInertiaWorld * joint.holdImpulse;
            b.angularVelocity += b.invInertiaWorld * joint.holdImpulse;
        }
    }
}

void solveJointPass(
    std::vector<PhysicsBody>& bodies,
    std::vector<JointConstraint>& joints,
    const JointSprings& springs,
    bool useBias
) {
    for (JointConstraint& joint : joints) {
        if (!joint.solvable && !joint.holdSolvable) continue;

        PhysicsBody& a = bodies[joint.bodyA];
        PhysicsBody& b = bodies[joint.bodyB];

        // The hold before the anchors, so the anchors have the last word on where the pair is.
        if (joint.holdSolvable) {
            const glm::vec3 spin = b.angularVelocity - a.angularVelocity;
            const glm::vec3 target = useBias ? spin + joint.holdBias : spin;
            const float massScale    = useBias ? springs.hold.massScale    : 1.0f;
            const float impulseScale = useBias ? springs.hold.impulseScale : 0.0f;

            const glm::vec3 previous = joint.holdImpulse;
            joint.holdImpulse = withinLimit(
                previous - massScale * (joint.holdMass * target) - impulseScale * previous,
                joint.holdLimit
            );
            const glm::vec3 applied = joint.holdImpulse - previous;
            a.angularVelocity -= a.invInertiaWorld * applied;
            b.angularVelocity += b.invInertiaWorld * applied;
        }

        if (!joint.solvable) continue;

        const SoftConstraint& soft = springs.anchor;

        // The leak is what keeps the spring from storing energy across passes.
        const glm::vec3 relative = velocityAt(b, joint.anchorB) - velocityAt(a, joint.anchorA);
        const glm::vec3 target = useBias ? relative + joint.bias : relative;
        const float massScale    = useBias ? soft.massScale    : 1.0f;
        const float impulseScale = useBias ? soft.impulseScale : 0.0f;

        const glm::vec3 impulse = -massScale * (joint.mass * target) - impulseScale * joint.impulse;
        joint.impulse += impulse;

        applyImpulse(a, impulse, joint.anchorA, -1.0f);
        applyImpulse(b, impulse, joint.anchorB, +1.0f);
    }
}

} // namespace Vkm::Engine
