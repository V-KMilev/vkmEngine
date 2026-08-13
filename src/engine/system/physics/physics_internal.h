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
 * @brief World-space pose frame for a body, cached at gather so writeback can
 *        map the solved world pose back to the entity's local Transform.
 *
 * `parented == false` means the body is a hierarchy root (its local Transform is
 * already the world pose - the fast path, no conversion).
 */
struct BodyFrame {
    bool      parented       = false;
    glm::mat4 parentWorldInv = glm::mat4(1.0f);                    ///< inverse(parent WorldTransform.model)
    glm::quat parentRot      = {1.0f, 0.0f, 0.0f, 0.0f};           ///< parent world-space rotation
};

/**
 * @brief A box placed in world space - the unit the box-box narrowphase consumes.
 * A collider expands into one of these per child box.
 */
struct SubShape {
    glm::vec3 center      = {0.0f, 0.0f, 0.0f};
    glm::quat rotation    = {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

} // namespace Engine
