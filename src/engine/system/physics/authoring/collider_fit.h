#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief Max detail, in grid cells per axis, for fitBoxesToMesh.
 *
 * The fit is a per-column scanline, roughly O(detail^2 * triangles), and the
 * box count grows with detail^2 as well - so raising detail costs both at fit
 * time and on every tick thereafter.
 */
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
 *
 * Every part returned is a ColliderShape::Box. Fitting capsules to a mesh is a
 * different problem (a medial axis, not a scanline) and is not attempted.
 */
std::vector<ColliderPart> fitBoxesToMesh(const MeshAsset& mesh, int detail, const glm::vec3& scale);

} // namespace Vkm::Engine
