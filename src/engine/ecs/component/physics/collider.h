#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/reflect.h"
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
    Count          ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief One primitive of a collider, in the entity's local frame.
 *
 * Placed by the entity Transform's position and rotation plus `center`. Sizes are absolute: the solver
 * ignores Transform scale. A capsule's segment runs along local +Y; its total height is
 * 2*(halfHeight + radius), and halfHeight 0 is a sphere.
 */
struct ColliderPart {
    ColliderShape shape       = ColliderShape::Box;   ///< Which fields below are live
    glm::vec3     center      = {0.0f, 0.0f, 0.0f};   ///< Local offset from the entity origin
    glm::vec3     halfExtents = {0.5f, 0.5f, 0.5f};   ///< Box: half-sizes
    float         radius      = 0.5f;                 ///< Capsule: sweep radius
    float         halfHeight  = 0.5f;                 ///< Capsule: half the segment, caps excluded
    MeshHandle    mesh        = {};                   ///< Mesh: whose triangles; stored by name
    /// Mesh: applied to every vertex, as Transform scale is not
    glm::vec3     meshScale   = {1.0f, 1.0f, 1.0f};
};

/**
 * @brief One node of a triangle mesh's bounding hierarchy.
 *
 * A leaf names a run of triangles; an interior node names its right child, its left is next.
 */
struct MeshNode {
    glm::vec3 min = {0.0f, 0.0f, 0.0f};
    glm::vec3 max = {0.0f, 0.0f, 0.0f};

    uint32_t firstTriangle = 0;  ///< Leaf: first triangle; interior: unused
    uint32_t triangleCount = 0;  ///< 0 marks an interior node
    uint32_t rightChild    = 0;  ///< Interior: the far child; left is this + 1
};

/**
 * @brief What a Collider's triangles were built from, so a change to any of it rebuilds them.
 */
struct MeshColliderSource {
    uint64_t  uid     = 0;                    ///< The mesh asset, by Resource::uid; zero for none.
    uint64_t  version = 0;                    ///< Its Resource::version when built.
    glm::vec3 scale   = {0.0f, 0.0f, 0.0f};   ///< The part's meshScale when built.
};

/**
 * @brief Collision geometry attached to an entity, evaluated in its Transform frame.
 *
 * One part for a simple collider, many for a mesh-fitted one ("Fit to Mesh"). The narrowphase runs per
 * pair of parts (see ColliderProxy). A parented body's pose is its world transform, walked at gather.
 */
struct Collider {
    /// One or more parts; a single unit box by default.
    std::vector<ColliderPart> parts = { ColliderPart{} };

    /**
     * @brief The mesh part's triangle corners, three a triangle, in the part's frame, scaled by meshScale.
     *
     * Derived: syncMeshCollider (system/physics/authoring/mesh_collider.h) rebuilds them whenever the
     * mesh or scale differs from meshBuiltFrom; a scene file holds only the mesh's name. Kept here
     * because a query is handed the scene alone.
     */
    std::vector<glm::vec3> meshPoints;

    std::vector<MeshNode> meshNodes;      ///< Bounding hierarchy over meshPoints, built with them.
    MeshColliderSource    meshBuiltFrom;  ///< What the two above were built from.

    bool isTrigger = false;  ///< Reports overlaps as TriggerEvents; gets no impulse response.
    /// False: inert - no broadphase entry, no contacts, no debug draw.
    bool enabled   = true;
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::ColliderShape, "Box", "Capsule", "Mesh")

// Every shape's fields are reflected whatever the tag, so switching a part's shape and back keeps
// what it was authored with.
VKM_REFLECT_BEGIN(::Vkm::Engine::ColliderPart)
    VKM_F(shape)
    VKM_F(center)
    VKM_F(halfExtents)
    VKM_F(radius)
    VKM_F(halfHeight)
    VKM_F(mesh)
    VKM_F(meshScale)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Collider)
    VKM_F(isTrigger)
    VKM_F(enabled)
    VKM_F(parts)
VKM_REFLECT_END()
