#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/collider.h"

namespace Engine {

struct MeshAsset;

/// Max detail (grid cells per axis) for fitBoxesToMesh. The fit is a per-column
/// scanline (~O(detail^2 * triangles)); box count also grows with detail^2, so
/// high detail is heavier both to fit and at run time.
inline constexpr int COLLIDER_FIT_MAX_DETAIL = 64;

/**
 * @brief Approximate a mesh's solid volume with a set of local-space boxes.
 *
 * For each (y, z) cell of a detail x detail grid, casts an axis-aligned ray
 * along X and pairs the sorted surface crossings into inside spans; each span
 * becomes one box - exact along X, one cell thick in Y and Z. @p scale bakes
 * the entity Transform scale into the box centres and sizes (the solver ignores
 * Transform scale). @p detail is clamped to [1, COLLIDER_FIT_MAX_DETAIL];
 * detail == 1 returns a single box (the scaled bounds). Never returns empty -
 * a non-watertight or degenerate mesh falls back to one bounds-sized box.
 */
std::vector<ColliderBox> fitBoxesToMesh(const MeshAsset& mesh, int detail, const glm::vec3& scale);

} // namespace Engine
