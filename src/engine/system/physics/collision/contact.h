#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Engine {

/// Maximum contact points generated per overlapping pair (a box face contact
/// needs at most four).
inline constexpr int MAX_CONTACTS_PER_MANIFOLD = 4;

/**
 * @brief A single collision contact point in world space.
 *
 * normal points from body A toward body B (push A along -normal, B along
 * +normal). penetration is the positive overlap depth along the normal.
 * The accumulated impulses persist across the solver's iterations within one
 * tick so successive passes converge instead of fighting each other.
 */
struct Contact {
    glm::vec3 point  = {0.0f, 0.0f, 0.0f};  ///< Contact position (world)
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};  ///< Unit normal, A -> B
    float penetration = 0.0f;               ///< Positive overlap depth

    float normalImpulse   = 0.0f;           ///< Accumulated normal impulse this tick
    float tangentImpulse  = 0.0f;           ///< Accumulated friction impulse this tick
    float restitutionBias = 0.0f;           ///< Target separation speed (set once, pre-solve)
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

} // namespace Engine
