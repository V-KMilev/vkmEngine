#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "system/physics/collision/contact.h"

namespace Vkm::Engine {

/**
 * @brief Per-tick dynamic state the solver reads and writes, by index.
 *
 * Decoupled from the Scene: PhysicsSystem caches one of these per body each tick,
 * the solver resolves contacts against them, and the system writes the results
 * back to Rigidbody / Transform.
 *
 * invMass == 0 marks a static or kinematic body: it contributes infinite mass to
 * contacts and is never pushed.
 */
struct PhysicsBody {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};         ///< Body origin (entity world position)

    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};

    glm::mat3 invInertiaWorld = glm::mat3(0.0f);     ///< R * I_local^-1 * R^T for this tick
    float invMass = 0.0f;                            ///< 0 == static/kinematic

    float restitution = 0.2f;
    float friction    = 0.5f;
};

/**
 * @brief What the solver is told about the step it is solving.
 *
 * Two values, because only two ever vary: the project sets the iteration count
 * and the clock sets the step. Everything else the solver is tuned by - the
 * contact spring, its damping, the recovery ceiling, the restitution floor, the
 * joint correction rate - is a constant beside the code that reads it, which is
 * where a number nobody passes belongs.
 */
struct SolverParams {
    int   iterations = 8;            ///< PGS passes per tick.
    float dt         = 1.0f / 60.0f; ///< Fixed timestep, in seconds.
};

/**
 * @brief Resolve all contact manifolds with sequential impulses (PGS).
 *
 * One pass structure, not two. Each of params.iterations passes applies the
 * normal impulse, a soft position-recovery term folded into the same impulse,
 * and clamped Coulomb friction. Mutates bodies in place by the indices stored on
 * each manifold; the caller integrates the resulting velocities.
 *
 * The recovery term is a soft constraint - Catto's "soft step", as shipped in
 * Box2D v3. A contact is treated as a stiff, heavily damped spring, and the
 * three coefficients derived from (hertz, damping, dt) scale the impulse and
 * bleed off the accumulated one. That is what keeps penetration recovery from
 * adding kinetic energy, and so what lets one pass do the whole job: there is no
 * position solver, no shadow velocity and no penetration slop.
 *
 * Each contact's restitution target is taken once, from the approach speed
 * measured before the first pass. Recomputing it per iteration would read the
 * post-impulse, already-separating velocity, so the bias would vanish after
 * pass one and the remaining passes would drive the contact back to a resting
 * vn == 0 - cancelling the bounce, and stopping a moving body dead at a wall.
 */
void solveContacts(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    const SolverParams& params
);

/**
 * @brief Take back the separation velocity the recovery term added.
 *
 * Run **after the poses have been integrated**, which is the whole point of the
 * pair: the biased passes hand an overlapping pair a velocity that pushes it
 * apart, the integration spends that velocity on actually separating them, and
 * this removes what is left so the pair is not still flying apart next tick.
 *
 * Called before the integration it would simply cancel the recovery, and the
 * overlap would stay exactly where it was - which is a stack of boxes resting
 * inside one another.
 *
 * @param bodies Solver bodies, already integrated for this tick.
 * @param manifolds The same manifolds solveContacts resolved.
 * @param params The step being solved.
 */
void relaxContacts(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    const SolverParams& params
);

} // namespace Vkm::Engine
