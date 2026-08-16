#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief One box of a collider, in the entity's local frame.
 *
 * The collider is placed by the entity Transform (position + rotation); each box
 * adds a local centre offset on top of that. Half-extents are absolute - the
 * solver ignores Transform scale, so "Fit to Mesh" bakes it into both the centre
 * and the half-extents.
 */
struct ColliderBox {
    glm::vec3 center      = {0.0f, 0.0f, 0.0f};   ///< Local offset from the entity origin
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};   ///< Box half-sizes
};

/**
 * @brief Collision geometry attached to an entity, evaluated in its Transform frame.
 *
 * The shape is always a set of oriented boxes: one box for a simple collider,
 * many for a mesh-fitted one ("Fit to Mesh"). The narrowphase is box-vs-box
 * only - it runs once per child-box pair. Pose comes from the entity's
 * Transform (root-space == world for physics bodies).
 */
struct Collider {
    std::vector<ColliderBox> parts = { ColliderBox{} }; ///< The collision volume: one or more boxes. Default to a single unit box.
    bool isTrigger = false;                             ///< Generates contacts for queries but no impulse response.
    bool enabled   = true;                              ///< When false the collider is inert: no broadphase entry, no contacts, no debug draw.
};

VKM_REFLECT_BEGIN(Collider)
    VKM_F(isTrigger),
    VKM_F(enabled)
VKM_REFLECT_END()

} // namespace Engine
