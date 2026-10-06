#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/physics/collision/contact.h"

namespace Vkm::Engine {

struct JointConstraint;

/**
 * @brief Per-tick dynamic state the solver reads and writes, by index.
 *
 * Decoupled from the Scene. invMass == 0 marks a body this solve must not move (static, kinematic,
 * asleep, or another end's): infinite mass to contacts, never pushed.
 */
struct PhysicsBody {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};  ///< Body origin (entity world position)

    /**
     * @brief World orientation, as gathered and then as integrated.
     *
     * Not the local Transform's, which for a parented body is in the parent's frame; the inverse inertia
     * and joint lever arms are built from it.
     */
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};

    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};

    glm::mat3 invInertiaWorld = glm::mat3(0.0f);  ///< R * I_local^-1 * R^T for this tick
    float     invMass         = 0.0f;             ///< 0 == not moved by this solve

    float restitution = 0.2f;
    float friction    = 0.5f;
};

/**
 * @brief What the solver is told about the step it is solving.
 *
 * No defaults of its own: zero passes over a zero step solves nothing. Other tuning is constants beside
 * the code that reads them.
 */
struct SolverParams {
    int   iterations = 0;     ///< PGS passes per tick, from PhysicsSettings::solverIterations.
    float dt         = 0.0f;  ///< Fixed timestep, in seconds, from the clock.
};

/**
 * @brief Solve one tick: every contact and joint, and the poses they move.
 *
 * Soft-constraint projected Gauss-Seidel like Box2D v3's soft step, but one step per tick, not substeps.
 * The order is the point: biased passes (params.iterations, joints then contacts, so contacts have the
 * last word) give an overlapping pair a separating velocity; the poses integrate with it; unbiased relax
 * passes then remove what is left, so recovery adds no energy. Relaxed before integration, the overlap
 * would never close.
 *
 * Restitution targets are taken once, before the first pass; per pass they would read the already
 * separating velocity and cancel the bounce.
 *
 * Run after ContactCache::seed and with joint impulses from last tick. A body with no inverse mass is
 * never integrated.
 *
 * @param bodies Solver bodies, world space, mutated by the indices on each manifold and joint.
 * @param manifolds Contacts, seeded; left holding final impulses and lever arms for ContactCache::record.
 * @param joints Joints, prepared here; left holding the impulse each ended on.
 * @param params Iteration count and step.
 */
void solveStep(
    std::vector<PhysicsBody>& bodies,
    std::vector<ContactManifold>& manifolds,
    std::vector<JointConstraint>& joints,
    const SolverParams& params
);

} // namespace Vkm::Engine
