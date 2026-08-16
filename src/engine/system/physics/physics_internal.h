#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/collider.h"

namespace Engine {

/**
 * @brief Broadphase / narrowphase view of one collidable body, cached per tick.
 * `body` indexes the parallel solver-body array; `cullStatic` is true only for
 * permanently immovable bodies (static/kinematic) - a sleeping dynamic body is
 * NOT cullStatic, so it keeps generating contacts and stays supported.
 */
struct ColliderProxy {
    uint32_t body = 0;

    /**
     * @brief This body's boxes, as a span into the tick's shared parts buffer.
     *
     * A span rather than a copy of the Collider. Holding the component by value
     * meant every tick destroyed and reallocated one std::vector per body -
     * thousands of allocations a second for geometry that almost never changes.
     * Indices rather than pointers because the shared buffer may grow while
     * proxies are still being appended, and an index survives that.
     */
    uint32_t partsFirst = 0;
    uint32_t partsCount = 0;
    bool isTrigger = false;
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 aabbMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 aabbMax = {0.0f, 0.0f, 0.0f};
    bool cullStatic = false;
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
    float     invMass         = 0.0f;                             ///< 1/mass this tick; 0 = static/kinematic
    glm::mat3 invInertiaLocal = glm::mat3(0.0f);                  ///< body-local inverse inertia; 0 = no rotational response
    glm::quat worldRot        = {1.0f, 0.0f, 0.0f, 0.0f};         ///< body world-space rotation this tick
    glm::mat4 parentWorldInv  = glm::mat4(1.0f);                  ///< inverse(parent WorldTransform.model)
    glm::quat parentRot       = {1.0f, 0.0f, 0.0f, 0.0f};         ///< parent world-space rotation
};

} // namespace Engine
