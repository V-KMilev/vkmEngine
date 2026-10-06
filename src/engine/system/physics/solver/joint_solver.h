#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "system/physics/solver/solver.h"
#include "system/physics/solver/solver_math.h"

namespace Vkm::Engine {

/**
 * @brief One joint, resolved to the two solver bodies it constrains.
 *
 * The tick's view of a Joint, as ContactManifold is of an overlap: ids and anchors are already body
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
     * A point joint removes all three degrees of freedom between the anchors; a distance joint the one
     * along their line.
     */
    float distance = -1.0f;

    float stiffness = 1.0f;

    /**
     * @brief The solve's working state, built once per tick by prepareJoints.
     *
     * Constant across passes. A point joint's mass is the inverse effective mass; a distance joint's is
     * its scalar mass along the line times the projector onto it, leaving every direction across it free.
     */
    bool      solvable = false;               ///< False when nothing can move, or there is no line to hold
    glm::mat3 mass     = glm::mat3(0.0f);     ///< Impulse per unit of relative anchor velocity
    glm::vec3 bias     = {0.0f, 0.0f, 0.0f};  ///< Relative velocity that closes the drift, stiffness applied

    /**
     * @brief Accumulated over this tick's passes from last tick's final value, world space.
     */
    glm::vec3 impulse = {0.0f, 0.0f, 0.0f};

    float     holdTorque   = 0.0f;                    ///< Joint::holdTorque; 0 leaves the rotation free
    glm::quat heldRotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< B's rotation in A's frame that the hold keeps
    float     holdLimit    = 0.0f;                    ///< Most angular impulse it carries this tick

    bool      holdSolvable = false;               ///< False when free, or nothing can turn
    glm::mat3 holdMass     = glm::mat3(0.0f);     ///< Angular impulse per unit of relative spin
    glm::vec3 holdBias     = {0.0f, 0.0f, 0.0f};  ///< Relative spin that turns the pair back
    glm::vec3 holdImpulse  = {0.0f, 0.0f, 0.0f};  ///< Accumulated like impulse, world space, within holdLimit
};

/**
 * @brief The two springs a joint solves with: the anchors', and the softer hold on the angle.
 */
struct JointSprings {
    SolverMath::SoftConstraint anchor;
    SolverMath::SoftConstraint hold;
};

/**
 * @brief Build every joint's working state for the tick, and the joint springs.
 *
 * From the gathered poses. A joint that cannot move, has no invertible mass, or (distance) has
 * coincident anchors is unsolvable and skipped.
 *
 * Last tick's impulse is kept for a point joint, flattened onto the line for a distance joint, kept
 * untouched for an immovable one (so a sleeping chain wakes loaded), and dropped only where the geometry
 * leaves nothing to hold it along. A hold's impulse is kept within its limit.
 *
 * @param bodies Solver bodies, as gathered.
 * @param joints Joints to prepare, in place.
 * @param dt Fixed timestep, seconds.
 * @return The springs' coefficients at this step, for solveJointPass.
 */
JointSprings prepareJoints(
    const std::vector<PhysicsBody>& bodies,
    std::vector<JointConstraint>& joints,
    float dt
);

/**
 * @brief Apply what each joint held last tick before the first pass asks anything.
 *
 * From zero, the passes never quite arrive at a chain's load and it hangs stretched. Run after
 * prepareJoints, which makes the carried impulse applicable.
 *
 * @param bodies Solver bodies, mutated in place.
 * @param joints Joints prepared for this tick.
 */
void warmStartJoints(std::vector<PhysicsBody>& bodies, const std::vector<JointConstraint>& joints);

/**
 * @brief One Gauss-Seidel pass over every joint.
 *
 * Removes the relative motion each joint forbids and, when biased, closes drift softly; a held joint
 * also turns its pair back toward the held angle, within its limit. The impulses accumulate across
 * passes and both halves of the solve, so later passes correct rather than add.
 *
 * @param bodies Solver bodies, mutated in place.
 * @param joints Joints prepared for this tick.
 * @param springs The joint springs; read only when biased.
 * @param useBias Whether to close drift; unbiased only stops relative motion.
 */
void solveJointPass(
    std::vector<PhysicsBody>& bodies,
    std::vector<JointConstraint>& joints,
    const JointSprings& springs,
    bool useBias
);

} // namespace Vkm::Engine
