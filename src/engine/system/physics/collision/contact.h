#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Maximum contact points generated per overlapping pair (a box face contact
 * needs at most four).
 */
inline constexpr int MAX_CONTACTS_PER_MANIFOLD = 4;

/**
 * @brief A single collision contact point in world space.
 *
 * normal points from body A toward body B (push A along -normal, B along
 * +normal). penetration is the positive overlap depth along the normal.
 *
 * The accumulated impulses persist across the solver's iterations so successive
 * passes converge instead of fighting each other - and across ticks, seeded from
 * the last one by PhysicsSystem before the solve. A stack is the reason: the
 * bottom contact of five boxes carries the weight of four, and a solver that
 * starts every tick from zero has to rediscover that in its iteration budget.
 * It never quite does, so the stack sinks into itself and leans as the four
 * points of a face converge unevenly.
 */
struct Contact {
    glm::vec3 point  = {0.0f, 0.0f, 0.0f};  ///< Contact position (world)
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};  ///< Unit normal, A -> B
    float penetration = 0.0f;               ///< Positive overlap depth

    float normalImpulse   = 0.0f;           ///< Accumulated normal impulse, seeded from last tick
    float restitutionBias = 0.0f;           ///< Target separation speed (set once, pre-solve)

    /**
     * @brief Friction as two impulses along a basis fixed for the whole tick.
     *
     * Not one impulse along the direction the contact is sliding: that
     * direction is re-derived every pass, so the accumulated scalar means
     * something different each time it is read, and a pass that finds the
     * sliding reversed applies the whole of it the wrong way. In a tall stack
     * the lateral velocities are near zero and the direction flips freely,
     * which is a tower shaking itself apart.
     *
     * Two impulses on a basis built from the normal have one meaning for the
     * whole tick, are clamped together against the friction cone rather than
     * separately, and can be carried into the next tick as a vector.
     */
    glm::vec3 tangent1 = {0.0f, 0.0f, 0.0f};
    glm::vec3 tangent2 = {0.0f, 0.0f, 0.0f};
    float tangentImpulse1 = 0.0f;
    float tangentImpulse2 = 0.0f;

    /**
     * @brief Pre-solve constants: the lever arms and the normal-direction
     *        effective mass.
     *
     * Constant for the whole solve. Body positions do not move during the
     * iteration loops - only velocities do, and the pose is integrated later in
     * writeback - so rA, rB and the normal effective mass computed from them
     * cannot change between passes. Recomputing them per iteration cost ~50
     * flops each across 8 velocity and 8 position passes, for a value that was
     * already known.
     *
     * The *tangent* effective mass is deliberately not here: the friction
     * direction is re-derived each pass from the current relative motion, so it
     * genuinely varies.
     */
    glm::vec3 rA = {0.0f, 0.0f, 0.0f};
    glm::vec3 rB = {0.0f, 0.0f, 0.0f};
    float normalMass = 0.0f;
};

/**
 * @brief Contacts between one pair of bodies for a single tick.
 *
 * bodyA / bodyB index into the PhysicsSystem's per-tick body snapshot, not the
 * Scene - the solver works purely against cached body state by index.
 */
struct ContactManifold {
    uint32_t bodyA = 0;   ///< Index into the tick body snapshot
    uint32_t bodyB = 0;   ///< Index into the tick body snapshot
    int count = 0;        ///< Number of valid entries in contacts[]
    Contact contacts[MAX_CONTACTS_PER_MANIFOLD];
};

} // namespace Vkm::Engine
