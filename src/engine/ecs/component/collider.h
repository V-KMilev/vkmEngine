#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Collision shape kinds supported in the first dynamics pass.
 *
 * Sphere and Box cover the inertia-tensor math cleanly and yield every pairwise
 * narrowphase case; Plane is an infinite immovable half-space used as the ground.
 * Capsule/mesh shapes are deliberately out of scope until a caller needs them.
 */
enum class ColliderShape {
    Sphere = 0,    ///< Radius around the entity origin
    Box    = 1,    ///< Local half-extents, rotated by the entity Transform
    Plane  = 2     ///< Infinite half-space: normal + signed distance from origin
};

/// Names in ColliderShape order - single source for serialization + editor combo.
inline constexpr const char* const COLLIDER_SHAPE_NAMES[] = {
    "Sphere", "Box", "Plane"
};

/**
 * @brief Collision geometry attached to an entity, evaluated in its Transform frame.
 *
 * Data-only component. Fields are reused per shape: a Sphere reads radius, a Box
 * reads halfExtents, a Plane reads planeNormal + planeOffset. Pose comes from the
 * entity's Transform (root-space == world for physics bodies).
 */
struct Collider {
    ColliderShape shape = ColliderShape::Box;

    float     radius      = 0.5f;                    ///< Sphere radius
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};      ///< Box half-sizes in local space

    glm::vec3 planeNormal = {0.0f, 1.0f, 0.0f};      ///< Plane unit normal (world space)
    float     planeOffset = 0.0f;                    ///< Plane signed distance along the normal

    bool isTrigger = false;                          ///< Generates contacts for queries but no impulse response
};

} // namespace Engine
