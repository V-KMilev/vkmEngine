#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_physics/bounds.h"
#include "vkm_physics/collision/mesh_bvh.h"
#include "vkm_physics/mass_model.h"
#include "vkm_physics/part.h"

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief The primitive a collider part is made of.
 *
 * Says which of the part's fields the narrowphase reads. Serialized by name.
 */
enum class ColliderShape : uint8_t {
    Box     = 0,   ///< Oriented box; reads center + halfExtents.
    Capsule = 1,   ///< Swept segment along local +Y; reads center + radius + halfHeight.
    Mesh    = 2,   ///< Triangle soup; reads center + mesh + meshScale.
    Terrain = 3,   ///< The entity's Terrain grid as a triangle soup, at the entity: its center stays zero.
    Count          ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief Whether a part of @p shape collides as Collider::triangles: a mesh's triangles, or a
 *        terrain's.
 *
 * @param shape The part's shape.
 * @return True for Mesh and Terrain.
 */
inline bool isTriangles(ColliderShape shape) {
    return shape == ColliderShape::Mesh || shape == ColliderShape::Terrain;
}

/**
 * @brief One primitive of a collider, in the entity's local frame.
 *
 * Placed by the entity Transform's position and rotation plus `center`. Sizes are absolute: the solver
 * ignores Transform scale, which the editor writes into them as an entity is scaled (rescaleParts). A
 * capsule's segment runs along local +Y; its total height is 2*(halfHeight + radius), and halfHeight 0
 * is a sphere.
 */
struct ColliderPart {
    ColliderShape shape       = ColliderShape::Box;  ///< Which fields below are live
    glm::vec3     center      = {0.0f, 0.0f, 0.0f};  ///< Local offset from the entity origin
    glm::vec3     halfExtents = {0.5f, 0.5f, 0.5f};  ///< Box: half-sizes
    float         radius      = 0.5f;                ///< Capsule: sweep radius
    float         halfHeight  = 0.5f;                ///< Capsule: half the segment, caps excluded
    MeshHandle    mesh        = {};                  ///< Mesh: whose triangles; stored by name
    glm::vec3     meshScale   = {1.0f, 1.0f, 1.0f};  ///< Mesh: applied to every vertex, as Transform
                                                     ///< scale is not
};

/**
 * @brief What a Collider's triangles were built from, so a change to any of it rebuilds them.
 */
struct MeshColliderSource {
    uint64_t  uid     = 0;                   ///< The mesh or terrain asset, by Resource::uid; zero for none.
    uint64_t  version = 0;                   ///< Its Resource::version when built.
    glm::vec3 scale   = {0.0f, 0.0f, 0.0f};  ///< The part's meshScale when built.
};

/**
 * @brief Collision geometry attached to an entity, evaluated in its Transform frame.
 *
 * One part for a simple collider, many for a mesh-fitted one ("Fit to Mesh", fitBoxesToMesh, which bakes
 * Transform scale into a grid of boxes and never fits capsules). Capsules are what characters wear, as a
 * box catches on every seam between floor boxes. One mesh part per collider: extra ones collide as the
 * first, and the log says so. The narrowphase runs per pair of parts (see ColliderProxy). A parented
 * body's pose is its world transform, walked at gather.
 */
struct Collider {
    std::vector<ColliderPart> parts = { ColliderPart{} };  ///< One or more parts; a single unit box by
                                                           ///< default.

    Physics::TriangleMesh triangles;      ///< The mesh or terrain part's triangles and their tree, in the
                                          ///< part's frame; a mesh's scaled by meshScale. Derived:
                                          ///< syncMeshCollider and syncTerrainCollider
                                          ///< (ecs/component/physics/mesh_collider.h) rebuild them
                                          ///< whenever their source differs from meshBuiltFrom, and a
                                          ///< scene file holds only the mesh's name. Kept here because a
                                          ///< query is handed the scene alone.
    MeshColliderSource    meshBuiltFrom;  ///< What the triangles were built from.

    bool isTrigger = false;  ///< Reports overlaps as TriggerEvents; gets no impulse response. Senses
                             ///< what moves, never a static body, or it would report the floor.
    bool enabled   = true;   ///< False: inert - no broadphase entry, no contacts, no debug draw; for
                             ///< pooled objects.
};

/**
 * @brief @p part as the physics reads it: a Mesh and a Terrain part are both Triangles.
 *
 * @param part One part of a Collider.
 * @return The physics' part.
 */
Physics::Part physicsPartOf(const ColliderPart& part);

/**
 * @brief Every part's bound in the body's own frame, together, as the physics bounds it.
 *
 * @param collider Collider whose parts are bounded.
 * @return The bound; a point at the origin with no parts.
 */
Physics::AABB colliderLocalBounds(const Collider& collider);

/**
 * @brief How a body's mass lies, from its collider's parts (Physics::MassModel::of).
 *
 * @param collider The body's collider; null for none.
 * @return The model; a point at the origin with no collider.
 */
Physics::MassModel massModelOf(const Collider* collider);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::ColliderShape, "Box", "Capsule", "Mesh", "Terrain")

// Every shape's fields are reflected whatever the tag, so switching a part's shape and back keeps
// what it was authored with.
VKM_REFLECT_BEGIN(::Vkm::Engine::ColliderPart)
    VKM_F(shape)
    VKM_F(center, "Offset from the entity's origin")
    VKM_F(halfExtents, 0.001f, 1000.0f, "A box's half sizes")
    VKM_F(radius, 0.001f, 1000.0f, "A capsule's radius")
    VKM_F(halfHeight, 0.0f, 1000.0f, "Half a capsule's segment, caps excluded: 0 is a sphere")
    VKM_F(mesh, "The mesh whose triangles it is")
    VKM_F(meshScale, 0.001f, 1000.0f, "Scales every vertex, as the Transform's scale does not")
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Collider)
    VKM_F(isTrigger, "Reports overlaps as events and is never pushed")
    VKM_F(enabled, "Off: inert, with no contacts")
    VKM_F(parts)
VKM_REFLECT_END()
