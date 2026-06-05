#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "system/physics/collision/contact.h"

namespace Engine {

/**
 * @brief Per-tick dynamic state the solver reads and writes, by index.
 *
 * Decoupled from the Scene: PhysicsSystem caches one of these per body each tick,
 * the solver resolves contacts against them, and the system writes the results
 * back to Rigidbody / Transform. pseudoLinear / pseudoAngular hold the
 * split-impulse position-correction velocities - integrated into the pose
 * alongside the real velocities but never persisted, so penetration is removed
 * without injecting energy into the simulation.
 *
 * invMass == 0 marks a static or kinematic body: it contributes infinite mass to
 * contacts and is never pushed.
 */
struct PhysicsBody {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};         ///< Centre of mass (world)

    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};

    glm::vec3 pseudoLinear  = {0.0f, 0.0f, 0.0f};    ///< Split-impulse correction velocity
    glm::vec3 pseudoAngular = {0.0f, 0.0f, 0.0f};

    glm::mat3 invInertiaWorld = glm::mat3(0.0f);     ///< R * I_local^-1 * R^T for this tick
    float invMass = 0.0f;                            ///< 0 == static/kinematic

    float restitution = 0.2f;
    float friction    = 0.5f;
};

/**
 * @brief Tunables for the sequential-impulse contact solver.
 */
struct SolverParams {
    int   iterations = 8;        ///< PGS passes per tick
    float dt = 1.0f / 60.0f;     ///< Fixed timestep
    float baumgarte = 0.2f;      ///< Position-correction stiffness [0,1]
    float penetrationSlop = 0.005f;   ///< Allowed overlap before correction kicks in
    float restitutionThreshold = 1.0f; ///< Below this approach speed, ignore bounce
};

/**
 * @brief Resolve all contact manifolds with sequential impulses (PGS).
 *
 * Runs params.iterations passes of normal + Coulomb-friction impulses with
 * restitution, then a separate split-impulse pass that fills pseudoLinear /
 * pseudoAngular to push the bodies apart. Mutates bodies in place by the indices
 * stored on each manifold; the caller integrates the resulting velocities.
 */
void solveContacts(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    const SolverParams& params
);

} // namespace Engine
