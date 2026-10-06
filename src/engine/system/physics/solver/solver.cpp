#include "system/physics/solver/solver.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/physics/solver/joint_solver.h"
#include "system/physics/solver/solver_math.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

/// Below this approach speed a contact does not bounce, however elastic it is.
constexpr float RESTITUTION_THRESHOLD = 1.0f;

/**
 * @brief How stiff a contact is, as a spring frequency in hertz.
 *
 * Box2D's 30 Hz. softness() clamps it to half the tick rate, above which a spring oscillates; the two
 * meet at a 60 Hz tick. The half-rate bound is this engine's, not Box2D's.
 */
constexpr float CONTACT_HERTZ = 30.0f;

/// Damping ratio of that spring. Well above 1: a contact must not ring.
constexpr float CONTACT_DAMPING = 10.0f;

/**
 * @brief Passes that take back the velocity the recovery terms added.
 *
 * Few: they undo one tick's bias over impulses that have already converged.
 */
constexpr int RELAX_PASSES = 3;

/**
 * @brief Ceiling on the separation speed penetration recovery may ask for.
 *
 * Recovery scales with depth, so a deep overlap (a spawn inside geometry, a teleport) would launch.
 */
constexpr float MAX_RECOVERY_SPEED = 3.0f;

/**
 * @brief Combined material response for a contacting pair.
 *
 * The max, so a bouncy ball off a dead floor still bounces. combineFriction is the geometric mean.
 *
 * @param a One body of the pair.
 * @param b The other.
 * @return The pair's restitution.
 */
float combineRestitution(const PhysicsBody& a, const PhysicsBody& b) {
    return std::max(a.restitution, b.restitution);
}

float combineFriction(const PhysicsBody& a, const PhysicsBody& b) {
    return std::sqrt(std::max(0.0f, a.friction * b.friction));
}

/**
 * @brief Whether the solve can move either body of a pair.
 *
 * Sleepers, or a sleeper on a static floor, cannot: passes would only leak their load, so the pair is
 * skipped and its impulses pass to the cache untouched.
 *
 * @param bodies Solver bodies the manifold indexes.
 * @param manifold The pair asked about.
 * @return True when either body has inverse mass.
 */
bool movable(const std::vector<PhysicsBody>& bodies, const ContactManifold& manifold) {
    return bodies[manifold.bodyA].invMass > 0.0f || bodies[manifold.bodyB].invMass > 0.0f;
}

using SolverMath::applyImpulse;
using SolverMath::effectiveMass;
using SolverMath::SoftConstraint;
using SolverMath::softness;
using SolverMath::velocityAt;

/**
 * @brief One Gauss-Seidel pass over every contact: normal impulse, then friction.
 *
 * Both accumulate across passes, so a later pass corrects rather than adds.
 *
 * @param bodies Solver bodies the impulses are applied to.
 * @param manifolds Contacts to resolve.
 * @param soft The contact spring's coefficients; read only when biased.
 * @param useBias Whether to push an overlap apart; unbiased only stops the approach.
 */
void solvePass(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    const SoftConstraint& soft,
    bool useBias
) {
    for (ContactManifold& manifold : manifolds) {
        if (!movable(bodies, manifold)) continue;

        // By reference: a manifold's two bodies are never the same one.
        PhysicsBody& a = bodies[manifold.bodyA];
        PhysicsBody& b = bodies[manifold.bodyB];
        const float friction = combineFriction(a, b);

        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            const glm::vec3& rA = contact.rA;
            const glm::vec3& rB = contact.rB;

            const glm::vec3 relVel = velocityAt(b, rB) - velocityAt(a, rA);
            const float vn = glm::dot(relVel, contact.normal);

            // Negative while overlapping: the impulse cancels that velocity, pushing them apart.
            const float separation = -contact.penetration;
            const float bias = useBias ? std::max(soft.biasRate * separation, -MAX_RECOVERY_SPEED) : 0.0f;
            const float massScale    = useBias ? soft.massScale    : 1.0f;
            const float impulseScale = useBias ? soft.impulseScale : 0.0f;

            float jn = -contact.normalMass * massScale * (vn - contact.restitutionBias + bias)
                - impulseScale * contact.normalImpulse;

            // Clamp the *accumulated* impulse non-negative; apply only the delta.
            const float oldNormal = contact.normalImpulse;
            contact.normalImpulse = std::max(0.0f, oldNormal + jn);
            jn = contact.normalImpulse - oldNormal;

            const glm::vec3 normalImpulse = jn * contact.normal;
            applyImpulse(a, normalImpulse, rA, -1.0f);
            applyImpulse(b, normalImpulse, rB, +1.0f);

            // Clamped to the cone as one vector: per axis bounds it by a square, so a box would slide
            // faster corner-on than face-on.
            const glm::vec3 relVelT = velocityAt(b, rB) - velocityAt(a, rA);

            float wanted1 = contact.tangentImpulse1
                - contact.tangentMass1 * glm::dot(relVelT, contact.tangent1);
            float wanted2 = contact.tangentImpulse2
                - contact.tangentMass2 * glm::dot(relVelT, contact.tangent2);

            const float maxFriction = friction * contact.normalImpulse;
            const float asked = std::sqrt(wanted1 * wanted1 + wanted2 * wanted2);
            if (asked > maxFriction && asked > Physics::FRICTION_DEADZONE) {
                const float scale = maxFriction / asked;
                wanted1 *= scale;
                wanted2 *= scale;
            }

            const glm::vec3 frictionImpulse = (wanted1 - contact.tangentImpulse1) * contact.tangent1
                + (wanted2 - contact.tangentImpulse2) * contact.tangent2;
            contact.tangentImpulse1 = wanted1;
            contact.tangentImpulse2 = wanted2;

            applyImpulse(a, frictionImpulse, rA, -1.0f);
            applyImpulse(b, frictionImpulse, rB, +1.0f);
        }
    }
}

// The biased half: pre-solve, both warm starts, then the passes that close overlap and drift.
void solveBiased(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    std::vector<JointConstraint>& joints,
    const SolverParams& params
) {
    for (ContactManifold& manifold : manifolds) {
        PhysicsBody& a = bodies[manifold.bodyA];
        PhysicsBody& b = bodies[manifold.bodyB];
        const bool solved = movable(bodies, manifold);
        const float restitution = combineRestitution(a, b);
        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            contact.rA = contact.point - a.position;
            contact.rB = contact.point - b.position;

            // From the normal alone, so a persisting contact keeps the frame its impulses were measured in.
            const glm::vec3 seed = std::abs(contact.normal.x) < 0.9f
                ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            contact.tangent1 = glm::normalize(glm::cross(seed, contact.normal));
            contact.tangent2 = glm::cross(contact.normal, contact.tangent1);

            contact.tangentImpulse1 = glm::dot(contact.warmFriction, contact.tangent1);
            contact.tangentImpulse2 = glm::dot(contact.warmFriction, contact.tangent2);

            // The cache records the arms and carried impulses; the rest only visited pairs need.
            if (!solved) continue;

            contact.normalMass = effectiveMass(a, b, contact.rA, contact.rB, contact.normal);
            contact.tangentMass1 = effectiveMass(a, b, contact.rA, contact.rB, contact.tangent1);
            contact.tangentMass2 = effectiveMass(a, b, contact.rA, contact.rB, contact.tangent2);

            const glm::vec3 relVel = velocityAt(b, contact.rB) - velocityAt(a, contact.rA);
            const float vn = glm::dot(relVel, contact.normal);
            contact.restitutionBias = (-vn > RESTITUTION_THRESHOLD) ? -restitution * vn : 0.0f;
        }
    }

    // Warm start: starting from last tick's answer lets a stack hold within the iteration budget
    // instead of sinking a little every tick.
    for (ContactManifold& manifold : manifolds) {
        if (!movable(bodies, manifold)) continue;

        PhysicsBody& a = bodies[manifold.bodyA];
        PhysicsBody& b = bodies[manifold.bodyB];
        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            const glm::vec3 seeded = contact.normalImpulse * contact.normal
                + contact.tangentImpulse1 * contact.tangent1
                + contact.tangentImpulse2 * contact.tangent2;
            applyImpulse(a, seeded, contact.rA, -1.0f);
            applyImpulse(b, seeded, contact.rB, +1.0f);
        }
    }

    const JointSprings jointSprings = prepareJoints(bodies, joints, params.dt);
    warmStartJoints(bodies, joints);
    const SoftConstraint contactSoft = softness(CONTACT_HERTZ, CONTACT_DAMPING, params.dt);

    const bool useBias = true;
    for (int iter = 0; iter < params.iterations; ++iter) {
        solveJointPass(bodies, joints, jointSprings, useBias);
        solvePass(bodies, manifolds, contactSoft, useBias);
    }
}

// Both halves, so a body a degree into the floor is turned out as well as lifted.
void integratePoses(std::vector<PhysicsBody>& bodies, float dt) {
    for (PhysicsBody& body : bodies) {
        if (body.invMass == 0.0f) continue;
        body.position += body.linearVelocity * dt;
        const glm::vec3& w = body.angularVelocity;
        const glm::quat spin(0.0f, w.x, w.y, w.z);
        body.rotation = glm::normalize(body.rotation + 0.5f * spin * body.rotation * dt);
    }
}

void relax(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    std::vector<JointConstraint>& joints
) {
    const bool useBias = false;
    for (int iter = 0; iter < RELAX_PASSES; ++iter) {
        solveJointPass(bodies, joints, JointSprings{}, useBias);
        solvePass(bodies, manifolds, SoftConstraint{}, useBias);
    }
}

} // namespace

void solveStep(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    std::vector<JointConstraint>& joints,
    const SolverParams& params
) {
    solveBiased(bodies, manifolds, joints, params);
    integratePoses(bodies, params.dt);
    relax(bodies, manifolds, joints);
}

} // namespace Vkm::Engine
