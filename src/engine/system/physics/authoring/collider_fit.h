#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

struct MeshAsset;

inline constexpr float MIN_HALF_EXTENT = 1e-3f;  ///< Smallest half-extent a fitted collider is given, in
                                                 ///< meters. A millimeter, so a box fitted to flat geometry
                                                 ///< has an inside to push out of.

inline constexpr int COLLIDER_FIT_MAX_DETAIL = 64;  ///< Max detail, in grid cells per axis, for
                                                    ///< fitBoxesToMesh. Fit time is roughly O(detail^2 *
                                                    ///< triangles) and the box count grows with detail^2, so
                                                    ///< raising it costs on every tick too.

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

/**
 * @brief @p parts carried from the Transform scale @p from to @p to, since ColliderPart is unscaled.
 *
 * What a fit bakes in at the scale it is given, for a scale changed after it. A center and a mesh's scale
 * take the change with its sign, so a mirrored part sits mirrored and a mesh's triangles mirror with the
 * drawn mesh; a box's half extents take its size per axis. A capsule's total height takes Y's, and its
 * radius the geometric mean of X's and Z's, so two changes in a row give what the two at once do; a
 * capsule grown wider than tall is a sphere. A terrain's part stays where its terrain is. Sizes, and a
 * mesh's scale on each axis, keep MIN_HALF_EXTENT.
 *
 * @param parts The parts at @p from.
 * @param from  The scale they were made for; an axis at zero keeps its sizes, as nothing says them.
 * @param to    The scale to carry them to.
 * @return The parts at @p to.
 */
std::vector<ColliderPart> rescaleParts(
    std::vector<ColliderPart> parts,
    const glm::vec3& from,
    const glm::vec3& to
);

} // namespace Vkm::Engine
