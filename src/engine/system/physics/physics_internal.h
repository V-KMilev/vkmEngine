#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

/**
 * @brief Broadphase and narrowphase view of one collidable body, cached per tick.
 *
 * `body` indexes the parallel solver-body array. `immovable` is true only for
 * bodies nothing this tick can move - see Rigidbody::isImmovable, plus the
 * bodies a replay is not deciding - and a sleeping dynamic body is deliberately
 * not one: it keeps generating contacts, which is what keeps whatever rests on
 * it supported.
 */
struct ColliderProxy {
    uint32_t body = 0;

    /**
     * @brief This body's parts, as a span into the tick's shared parts buffer.
     *
     * A span rather than a copy of the Collider. Holding the component by value
     * meant every tick destroyed and reallocated one std::vector per body -
     * thousands of allocations a second for geometry that almost never changes.
     * Indices rather than pointers because the shared buffer may grow while
     * proxies are still being appended, and an index survives that.
     */
    uint32_t partsFirst = 0;
    uint32_t partsCount = 0;

    /**
     * @brief This body's parts already placed in world space, as two spans.
     *
     * Two monomorphic arrays rather than one tagged list, because the pair
     * loops in the narrowphase are quadratic and a shape test inside them
     * would run once per pairing instead of once per part.
     *
     * Filled at gather, once per body, rather than per pair: a floor appears in
     * as many pairs as there are things standing on it, and placing it into
     * world space per pair places it once for each of them. A mesh part is
     * absent from both - it is thousands of triangles and only the handful under
     * the other shape matter, so it is walked per pair against that shape's
     * bound, which is what it carries a tree for.
     */
    uint32_t boxFirst    = 0;
    uint32_t boxCount    = 0;
    uint32_t capsuleFirst = 0;
    uint32_t capsuleCount = 0;

    /**
     * @brief The component this proxy was built from, for what did not copy.
     *
     * A mesh part's hierarchy lives on the Collider and is thousands of nodes;
     * copying it per tick to reach it would cost more than the tree saves. Safe
     * for the tick, on the same grounds the parts span is: nothing adds or
     * removes a component between gather and the narrowphase.
     */
    const Collider* collider = nullptr;
    bool isTrigger = false;
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 aabbMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 aabbMax = {0.0f, 0.0f, 0.0f};
    bool immovable = false;

    // Copied off the Rigidbody at gather, so the pair loop reads one struct
    // rather than reaching back into the scene per candidate pair.
    int layer = 1;
    int collidesWith = ~0;
};

/**
 * @brief Per-tick body state cached at gather: the mass properties the solver
 *        runs on, plus the frame writeback maps the solved world pose back through.
 *
 * `parented == false` means the body is a hierarchy root (its local Transform is
 * already the world pose - the fast path, no conversion). `worldRot` is the
 * orientation the solver ran in, kept so anything rebuilding a world inertia
 * tensor mid-tick uses the same rotation gather did rather than the local
 * Transform's, which is the parent's frame for a parented body. `invMass` and
 * `invInertiaLocal` are re-derived from the Rigidbody + Collider every tick, so
 * editing mass or the shape takes effect without a separate "apply" step.
 */
struct BodyFrame {
    bool      parented        = false;
    bool      decided         = false;                            ///< This end decides where it goes; always true offline
    float     invMass         = 0.0f;                             ///< 1/mass this tick; 0 = static/kinematic
    glm::mat3 invInertiaLocal = glm::mat3(0.0f);                  ///< body-local inverse inertia; 0 = no rotational response
    glm::quat worldRot        = {1.0f, 0.0f, 0.0f, 0.0f};         ///< body world-space rotation this tick
    glm::mat4 parentWorld     = glm::mat4(1.0f);                  ///< Parent WorldTransform.model
    glm::quat parentRot       = {1.0f, 0.0f, 0.0f, 0.0f};         ///< parent world-space rotation
};

/**
 * @brief What one tick's contacts add up to for one body, in the two reductions
 *        Rigidbody publishes.
 *
 * Filled by the narrowphase and read after the solve, so it belongs to neither
 * phase alone: fixedUpdate owns one of these per body and threads it through.
 * Both normals are the surface's as it acts on THIS body, so the two bodies of
 * one contact see opposite directions.
 */
struct BodyContacts {
    // Seeded below any unit normal, so the first contact always replaces it. A
    // body held only by a vertical wall would otherwise keep the zero vector and
    // report support with no direction.
    glm::vec3 support = {0.0f, -2.0f, 0.0f};   ///< Most upward contact normal

    // Seeded at world up, whose horizontal part is zero, so any contact with a
    // horizontal direction replaces it and a body touching nothing but level
    // floor is left holding "nothing is in the way".
    glm::vec3 block = {0.0f, 1.0f, 0.0f};      ///< Most horizontal contact normal

    bool touched = false;                      ///< A resolved, non-trigger contact reached this body
};

} // namespace Vkm::Engine
