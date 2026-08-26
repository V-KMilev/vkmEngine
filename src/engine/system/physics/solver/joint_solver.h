#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "system/physics/solver/solver.h"

namespace Vkm::Engine {

/**
 * @brief One joint, resolved to the two solver bodies it constrains.
 *
 * The tick's view of a Joint component, the way ContactManifold is the tick's
 * view of an overlap: entity ids and local anchors have already become body
 * indices and lever arms, so the solver never looks at the scene.
 */
struct JointConstraint {
    uint32_t bodyA = 0;
    uint32_t bodyB = 0;

    glm::vec3 anchorA = {0.0f, 0.0f, 0.0f};  ///< World, relative to A's origin
    glm::vec3 anchorB = {0.0f, 0.0f, 0.0f};  ///< World, relative to B's origin

    /**
     * @brief Distance the anchors are held at; negative for a point joint.
     *
     * The two kinds differ only here, which is why they share a struct: a point
     * joint removes all three degrees of freedom between the anchors, and a
     * distance joint removes the one along the line between them.
     */
    float distance = -1.0f;

    float stiffness = 1.0f;
};

/**
 * @brief Remove the relative motion the joints forbid.
 *
 * Run after the contact solve, as its own set of passes over the same bodies.
 * Not interleaved with it: the contacts converge first, so a joint corrects
 * against velocities a surface has already had its say about, rather than
 * pulling a body into one the contact pass then pushes it out of.
 *
 * The cost of that order is that the last word belongs to the joints, so a
 * body pinned into a wall stays slightly inside it. The alternative trades the
 * body back and forth for as long as both constraints are unhappy, which is
 * visible where this is not.
 *
 * @param bodies Solver bodies, mutated in place.
 * @param joints Constraints to satisfy.
 * @param params Iteration count and the timestep the correction is scaled by.
 */
void solveJoints(
    std::vector<PhysicsBody>& bodies,
    const std::vector<JointConstraint>& joints,
    const SolverParams& params
);

} // namespace Vkm::Engine
