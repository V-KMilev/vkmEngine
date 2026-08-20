#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The primitive a collider part is made of.
 *
 * A part carries the fields for every shape and this tag says which of them the
 * narrowphase reads - the alternative, a parallel vector per shape, makes the
 * part list two lists that must stay in step. Serialized by name.
 */
enum class ColliderShape : uint8_t {
    Box     = 0,   ///< Oriented box; reads center + halfExtents.
    Capsule = 1,   ///< Swept segment along local +Y; reads center + radius + halfHeight.
    Count          ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief One primitive of a collider, in the entity's local frame.
 *
 * The collider is placed by the entity Transform (position + rotation); each
 * part adds a local centre offset on top of that. Sizes are absolute - the
 * solver ignores Transform scale, so "Fit to Mesh" bakes it into both the centre
 * and the half-extents.
 *
 * A capsule's segment runs along local +Y for halfHeight either side of the
 * centre and is swept by radius, so its total height is 2*(halfHeight + radius).
 * halfHeight 0 is a sphere, which is legal and needs no separate shape.
 */
struct ColliderPart {
    ColliderShape shape       = ColliderShape::Box;   ///< Which fields below are live
    glm::vec3     center      = {0.0f, 0.0f, 0.0f};   ///< Local offset from the entity origin
    glm::vec3     halfExtents = {0.5f, 0.5f, 0.5f};   ///< Box: half-sizes
    float         radius      = 0.5f;                 ///< Capsule: sweep radius
    float         halfHeight  = 0.5f;                 ///< Capsule: half the segment, caps excluded
};

/**
 * @brief Collision geometry attached to an entity, evaluated in its Transform frame.
 *
 * The shape is a set of oriented primitives: one part for a simple collider,
 * many for a mesh-fitted one ("Fit to Mesh"). The narrowphase runs once per
 * pair of parts, dispatching on the two shape tags. Pose comes from the entity's
 * Transform (root-space == world for physics bodies).
 */
struct Collider {
    std::vector<ColliderPart> parts = { ColliderPart{} }; ///< The collision volume: one or more parts. Default to a single unit box.
    bool isTrigger = false;                               ///< Generates contacts for queries but no impulse response.
    bool enabled   = true;                                ///< When false the collider is inert: no broadphase entry, no contacts, no debug draw.
};
} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::ColliderShape, "Box", "Capsule")

VKM_REFLECT_BEGIN(::Vkm::Engine::Collider)
    VKM_F(isTrigger),
    VKM_F(enabled)
VKM_REFLECT_END()
