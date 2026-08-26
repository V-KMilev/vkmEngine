#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

/**
 * @brief Build a hierarchy over @p triangles, reordering them to match.
 *
 * The reason a mesh collider is not simply a list of triangles: a level's
 * collision mesh is tens of thousands of them, and every pair against it would
 * otherwise test every one, every tick. The build sorts by median along the
 * widest axis, which costs nothing to describe and gives a tree within a
 * constant factor of the good ones.
 *
 * @param[in,out] triangles Points, three per triangle. Reordered in place so a
 *        node's triangles are contiguous; the caller's own indices into this
 *        span are invalidated, which is why it happens at build and never after.
 * @return The nodes, root first. Empty when @p triangles holds no whole one.
 */
std::vector<MeshNode> buildMeshBvh(std::vector<glm::vec3>& triangles);

/**
 * @brief Collect the triangles whose node overlaps @p min / @p max.
 *
 * @param nodes Hierarchy from buildMeshBvh.
 * @param min World-space lower corner of the region of interest.
 * @param max World-space upper corner.
 * @param[out] out Triangle indices, appended; not cleared.
 */
void queryMeshBvh(const std::vector<MeshNode>& nodes,
                  const glm::vec3& min, const glm::vec3& max,
                  std::vector<uint32_t>& out);

} // namespace Vkm::Engine
