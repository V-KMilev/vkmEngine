#include "system/physics/solver/solver.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "system/physics/solver/solver_math.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

/// Below this approach speed a contact does not bounce, however elastic it is.
constexpr float RESTITUTION_THRESHOLD = 1.0f;

/**
 * @brief How stiff a contact is, as a spring frequency in hertz.
 *
 * A contact is a spring the solver is allowed to make as stiff as the step can
 * carry: above a quarter of the tick rate a spring oscillates rather than
 * settles, so the effective value is clamped there. 30 Hz is Box2D's, and at a
 * 60 Hz tick the clamp is what actually binds.
 */
constexpr float CONTACT_HERTZ = 30.0f;

/// Damping ratio of that spring. Well above 1: a contact must not ring.
constexpr float CONTACT_DAMPING = 10.0f;

/**
 * @brief Passes that take back the velocity the recovery term added.
 *
 * Few, because they only have to undo one tick's worth of bias; the impulses
 * they run over have already converged.
 */
constexpr int RELAX_PASSES = 3;

/**
 * @brief Ceiling on the separation speed penetration recovery may ask for.
 *
 * Without it a deep overlap - a body spawned inside geometry, a teleport - is
 * resolved by launching it, because the recovery term scales with depth.
 */
constexpr float MAX_RECOVERY_SPEED = 3.0f;

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
// through SolverMath.
using SolverMath::applyImpulse;
using SolverMath::effectiveMass;
using SolverMath::velocityAt;

/**
 * @brief The three numbers that turn a contact into a stiff damped spring.
 *
 * Catto's soft-step coefficients. `bias` converts overlap into a target
 * separation speed, `massScale` softens the impulse that speed asks for, and
 * `impulseScale` bleeds off the accumulated impulse so the spring cannot store
 * energy across iterations - which is the property that lets one pass do what
 * split impulse needed a second solver and a shadow velocity to do.
 */
struct SoftConstraint {
    float biasRate     = 0.0f;
    float massScale    = 1.0f;
    float impulseScale = 0.0f;
};

SoftConstraint softness(float hertz, float damping, float dt) {
    if (dt <= 0.0f) return {};
    // However stiff a contact is asked to be, the step decides what it can
    // carry: half the tick rate is what this solver settles at, measured on
    // stacks, and it is the damping and the relax passes that let it hold that
    // - a spring this stiff without either rings instead of settling.
    const float clamped = std::min(hertz, 0.5f / dt);
    const float omega   = 2.0f * glm::pi<float>() * clamped;
    const float a1      = 2.0f * damping + dt * omega;
    const float a2      = dt * omega * a1;
    const float a3      = 1.0f / (1.0f + a2);
    return SoftConstraint{omega / a1, a2 * a3, a3};
}

/**
 * @brief One Gauss-Seidel pass over every contact: normal impulse, then friction.
 *
 * Both accumulate across passes, so a later pass corrects the impulse rather
 * than adding a second one.
 *
 * @param bodies Solver bodies the impulses are applied to.
 * @param manifolds Contacts to resolve.
 * @param soft The contact spring's coefficients; read only when biased.
 * @param useBias Whether the pass may ask for the overlap back. A biased pass
 *        pushes an overlap apart; an unbiased one only stops the approach.
 */
void solvePass(std::vector<PhysicsBody>& bodies, std::vector<ContactManifold>& manifolds,
               const SoftConstraint& soft, bool useBias) {

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

        // Separation is negative while the pair overlaps, so the bias is a
        // negative velocity the impulse cancels - which pushes them apart.
        // Capped, or a deep overlap launches whatever is inside it. A
        // relax pass asks for none of it: see the loop below.
        const float separation = -contact.penetration;
        const float bias = useBias
            ? std::max(soft.biasRate * separation, -MAX_RECOVERY_SPEED) : 0.0f;
        const float massScale    = useBias ? soft.massScale    : 1.0f;
        const float impulseScale = useBias ? soft.impulseScale : 0.0f;

        float jn = -contact.normalMass * massScale
                   * (vn - contact.restitutionBias + bias)
             - impulseScale * contact.normalImpulse;

        // Clamp the *accumulated* normal impulse to be non-negative;
        // apply only the delta needed to reach the new total.
        const float oldNormal = contact.normalImpulse;
        contact.normalImpulse = std::max(0.0f, oldNormal + jn);
        jn = contact.normalImpulse - oldNormal;

        const glm::vec3 normalImpulse = jn * contact.normal;
        applyImpulse(a, normalImpulse, rA, -1.0f);
        applyImpulse(b, normalImpulse, rB, +1.0f);

        // Friction on both axes of the contact plane, clamped against the
        // cone as one vector rather than per axis: an axis-by-axis clamp
        // bounds friction by a square, which lets a box slide faster
        // corner-on than face-on for no reason a surface has.
        const glm::vec3 relVelT = velocityAt(b, rB) - velocityAt(a, rA);
        const float kt1 = effectiveMass(a, b, rA, rB, contact.tangent1);
        const float kt2 = effectiveMass(a, b, rA, rB, contact.tangent2);

        float wanted1 = contact.tangentImpulse1
                  - kt1 * glm::dot(relVelT, contact.tangent1);
        float wanted2 = contact.tangentImpulse2
                  - kt2 * glm::dot(relVelT, contact.tangent2);

        const float maxFriction = friction * contact.normalImpulse;
        const float asked = std::sqrt(wanted1 * wanted1 + wanted2 * wanted2);
        if (asked > maxFriction && asked > Physics::FRICTION_DEADZONE) {
            const float scale = maxFriction / asked;
            wanted1 *= scale;
            wanted2 *= scale;
        }

        const glm::vec3 frictionImpulse =
              (wanted1 - contact.tangentImpulse1) * contact.tangent1
            + (wanted2 - contact.tangentImpulse2) * contact.tangent2;
        contact.tangentImpulse1 = wanted1;
        contact.tangentImpulse2 = wanted2;

        applyImpulse(a, frictionImpulse, rA, -1.0f);
        applyImpulse(b, frictionImpulse, rB, +1.0f);
        }
    }
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

            // A basis for the contact plane, from the normal alone: one normal
            // always yields the same two directions, so a contact that persists
            // keeps the frame its impulses were measured in.
            const glm::vec3 seed = std::abs(contact.normal.x) < 0.9f
                ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            contact.tangent1 = glm::normalize(glm::cross(seed, contact.normal));
            contact.tangent2 = glm::cross(contact.normal, contact.tangent1);

            const glm::vec3 relVel = velocityAt(b, contact.rB) - velocityAt(a, contact.rA);
            const float vn = glm::dot(relVel, contact.normal);
            contact.restitutionBias =
                (-vn > RESTITUTION_THRESHOLD) ? -restitution * vn : 0.0f;
        }
    }

    // What held this pair last tick, applied before the first pass asks anything.
    // Sequential impulses converge on the answer from wherever they start, and
    // starting from the answer is what lets a stack hold its shape inside an
    // iteration budget instead of sinking a little further every tick.
    for (ContactManifold& manifold : manifolds) {
        PhysicsBody& a = bodies[manifold.bodyA];
        PhysicsBody& b = bodies[manifold.bodyB];
        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            const glm::vec3 seeded = contact.normalImpulse   * contact.normal
                                   + contact.tangentImpulse1 * contact.tangent1
                                   + contact.tangentImpulse2 * contact.tangent2;
            applyImpulse(a, seeded, contact.rA, -1.0f);
            applyImpulse(b, seeded, contact.rB, +1.0f);
        }
    }

    const SoftConstraint soft =
        softness(CONTACT_HERTZ, CONTACT_DAMPING, params.dt);

    for (int iter = 0; iter < params.iterations; ++iter) {
        solvePass(bodies, manifolds, soft, /*useBias*/ true);
    }
}

void relaxContacts(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    const SolverParams& params
) {
    const SoftConstraint soft = softness(CONTACT_HERTZ, CONTACT_DAMPING, params.dt);
    for (int iter = 0; iter < RELAX_PASSES; ++iter) {
        solvePass(bodies, manifolds, soft, /*useBias*/ false);
    }
}

} // namespace Vkm::Engine
