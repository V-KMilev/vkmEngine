#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Maximum contact points per overlapping pair; a box face contact needs four.
 */
inline constexpr int MAX_CONTACTS_PER_MANIFOLD = 4;

/**
 * @brief A single collision contact point in world space.
 *
 * The accumulated impulses persist across solver passes, so they converge, and across ticks, seeded by
 * ContactCache::seed before the solve.
 */
struct Contact {
    glm::vec3 point       = {0.0f, 0.0f, 0.0f};  ///< Contact position (world)
    glm::vec3 normal      = {0.0f, 1.0f, 0.0f};  ///< Unit normal, A -> B
    float     penetration = 0.0f;                ///< Positive overlap depth

    float normalImpulse   = 0.0f;  ///< Accumulated normal impulse, seeded from last tick
    float restitutionBias = 0.0f;  ///< Target separation speed (set once, pre-solve)

    /**
     * @brief Friction as two impulses along a basis fixed for the whole tick.
     *
     * Not one impulse along the sliding direction: that is re-derived every pass, so a pass that finds
     * the sliding reversed would apply the whole accumulated scalar the wrong way.
     */
    glm::vec3 tangent1 = {0.0f, 0.0f, 0.0f};
    glm::vec3 tangent2 = {0.0f, 0.0f, 0.0f};
    float tangentImpulse1 = 0.0f;
    float tangentImpulse2 = 0.0f;

    /**
     * @brief Last tick's friction, as a world vector, waiting for a basis.
     *
     * Left by ContactCache::seed; the pre-solve resolves it into tangentImpulse1/2 once the basis exists.
     * Zero when the cache had nothing. A vector, because the scalars mean something only on the basis
     * they were measured on, and seed runs before this tick's basis is built.
     */
    glm::vec3 warmFriction = {0.0f, 0.0f, 0.0f};

    /**
     * @brief Pre-solve constants: the lever arms and the effective masses.
     *
     * No pass moves a body (poses integrate once, between the biased and relax passes), so these are
     * built once and read unchanged by every pass.
     */
    glm::vec3 rA = {0.0f, 0.0f, 0.0f};
    glm::vec3 rB = {0.0f, 0.0f, 0.0f};
    float normalMass   = 0.0f;
    float tangentMass1 = 0.0f;
    float tangentMass2 = 0.0f;
};

/**
 * @brief Contacts between one pair of bodies for a single tick.
 *
 * bodyA / bodyB index the PhysicsSystem's per-tick body snapshot, not the Scene.
 */
struct ContactManifold {
    uint32_t bodyA = 0;  ///< Index into the tick body snapshot
    uint32_t bodyB = 0;  ///< Index into the tick body snapshot
    int      count = 0;  ///< Number of valid entries in contacts[]
    Contact  contacts[MAX_CONTACTS_PER_MANIFOLD];
};

} // namespace Vkm::Engine
