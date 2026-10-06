#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief Max detail, in grid cells per axis, for fitBoxesToMesh.
 *
 * Fit time is roughly O(detail^2 * triangles) and the box count grows with detail^2, so raising it
 * costs on every tick too.
 */
inline constexpr int COLLIDER_FIT_MAX_DETAIL = 64;

/**
 * @brief Approximate a mesh's solid volume with a set of local-space boxes.
 *
 * Per (y, z) cell, a ray along X pairs the surface crossings into inside spans; each span is one box,
 * exact along X and one cell thick in Y and Z.
 *
 * @param mesh Source geometry, in its own local space.
 * @param detail Grid cells per axis, clamped to [1, COLLIDER_FIT_MAX_DETAIL]; 1 is the scaled bounds.
 * @param scale The entity's Transform scale, baked in, since ColliderPart is unscaled.
 * @return Never empty: flat bounds, under one triangle, a bad index or no span gives one bounds box.
 */
std::vector<ColliderPart> fitBoxesToMesh(const MeshAsset& mesh, int detail, const glm::vec3& scale);

} // namespace Vkm::Engine
