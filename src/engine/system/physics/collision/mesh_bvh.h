#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

/**
 * @brief Build a hierarchy over @p triangles, reordering them to match.
 *
 * @param[in,out] triangles Points, three per triangle; reordered so a node's are contiguous, which
 *        invalidates the caller's indices into them.
 * @return The nodes, root first. Empty when @p triangles holds no whole one.
 */
std::vector<MeshNode> buildMeshBvh(std::vector<glm::vec3>& triangles);

/**
 * @brief Collect the triangles whose node overlaps @p min / @p max.
 *
 * @param nodes Hierarchy from buildMeshBvh.
 * @param min Lower corner, in the tree's own space: the raw points, before the part's centre and the
 *        body's pose place them.
 * @param max Upper corner, same space.
 * @param[out] out Triangle indices, appended; not cleared.
 */
void queryMeshBvh(
    const std::vector<MeshNode>& nodes,
    const glm::vec3& min,
    const glm::vec3& max,
    std::vector<uint32_t>& out
);

/**
 * @brief Collect the triangles whose node a segment passes within @p grow of.
 *
 * For casts: a long ray's bounding box covers a slab of floor, while testing each node against the
 * segment opens only the nodes along the way.
 *
 * @param nodes Hierarchy from buildMeshBvh.
 * @param origin Segment start, in the tree's space.
 * @param dir Unit direction, in the tree's space.
 * @param length Segment length.
 * @param grow How far from the segment still counts: a swept sphere's radius.
 * @param[out] out Triangle indices, appended; not cleared.
 */
void queryMeshBvhSegment(
    const std::vector<MeshNode>& nodes,
    const glm::vec3& origin,
    const glm::vec3& dir,
    float length,
    float grow,
    std::vector<uint32_t>& out
);

} // namespace Vkm::Engine
