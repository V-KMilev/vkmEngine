#pragma once

#include <algorithm>
#include <vector>
#include <glm/glm.hpp>

#include "ecs/component/mesh.h"
#include "visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Occlusion culling: reject meshes that are fully hidden behind others.
 *
 * Placeholder: currently always returns true (no occlusion culling). A future
 * implementation would use a software depth buffer or Hi-Z and process in
 * front-to-back order.
 */
namespace OcclusionCuller {

/**
 * @brief True if the mesh is not fully occluded (currently always true; TODO).
 * @param mesh Mesh with world boundsMin/boundsMax.
 * @param context VisibilityContext.
 */
inline bool isVisible(
    const Mesh& mesh,
    const VisibilityContext& context
) {
    // TODO
    return true;
}

} // namespace OcclusionCuller

} // namespace Engine
