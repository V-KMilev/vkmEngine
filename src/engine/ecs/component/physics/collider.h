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
    Mesh    = 2,   ///< Triangle soup; reads center + meshFirst + meshCount.
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

    /**
     * @brief Mesh: the part's triangle corners, as a span into the Collider's
     *        buffer.
     *
     * A span rather than a vector per part, for the reason the proxy list uses
     * one: a part stays a plain value that copies without allocating. The
     * indices are the Collider's own, so a part is only meaningful beside it.
     */
    uint32_t      meshFirst   = 0;
    uint32_t      meshCount   = 0;
};

/**
 * @brief One node of a triangle mesh's bounding hierarchy.
 *
 * A leaf names a run of triangles; an interior node names its right child and
 * has its left implicitly next, which is what a depth-first build gives for
 * free and saves a second index per node.
 *
 * Component data rather than a system type: the nodes live on the Collider
 * beside the triangles they index, and the build and query that use them stay
 * with the physics system.
 */
struct MeshNode {
    glm::vec3 min = {0.0f, 0.0f, 0.0f};
    glm::vec3 max = {0.0f, 0.0f, 0.0f};

    uint32_t firstTriangle = 0;  ///< Leaf: first triangle; interior: unused
    uint32_t triangleCount = 0;  ///< 0 marks an interior node
    uint32_t rightChild    = 0;  ///< Interior: the far child; left is this + 1
};

/**
 * @brief Collision geometry attached to an entity, evaluated in its Transform frame.
 *
 * The shape is a set of oriented primitives: one part for a simple collider,
 * many for a mesh-fitted one ("Fit to Mesh"). The narrowphase runs once per
 * pair of parts, dispatching on the two shape tags. Pose comes from the entity's
 * world transform, which for a parented body is walked at gather rather than
 * read off its own Transform.
 */
struct Collider {
    std::vector<ColliderPart> parts = { ColliderPart{} }; ///< The collision volume: one or more parts. Default to a single unit box.

    /**
     * @brief Every mesh part's triangle corners, in the entity's frame.
     *
     * Read three points at a time, one triangle each. Corners rather than an
     * index buffer: it costs the duplicated corners of a shared edge and saves
     * the whole apparatus of keeping two arrays in step, and the narrowphase
     * only ever asks a triangle for its extreme point in a direction.
     */
    std::vector<glm::vec3> meshPoints;

    /**
     * @brief Bounding hierarchy over the mesh parts' triangles.
     *
     * Derived, not authored: rebuilt whenever it is empty and a Mesh part
     * exists, so it survives a scene load without being written to disk. A
     * level's collision mesh is tens of thousands of triangles and every pair
     * against it would otherwise test every one, every tick.
     */
    std::vector<MeshNode> meshNodes;
    bool isTrigger = false;                               ///< Generates contacts for queries but no impulse response.
    bool enabled   = true;                                ///< When false the collider is inert: no broadphase entry, no contacts, no debug draw.
};
} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::ColliderShape, "Box", "Capsule", "Mesh")

// Every shape's fields are reflected whatever the tag says, so switching a part
// to a capsule in the inspector and back does not quietly forget the
// half-extents it was authored with.
VKM_REFLECT_BEGIN(::Vkm::Engine::ColliderPart)
    VKM_F(shape),
    VKM_F(center),
    VKM_F(halfExtents),
    VKM_F(radius),
    VKM_F(halfHeight),
    VKM_F(meshFirst),
    VKM_F(meshCount)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Collider)
    VKM_F(isTrigger),
    VKM_F(enabled),
    VKM_F(parts),
    VKM_F(meshPoints)
VKM_REFLECT_END()
