#pragma once

#include <glm/glm.hpp>

#include "visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Occlusion culling: reject AABBs that are fully hidden behind others.
 *
 * Placeholder: currently always returns true (no occlusion culling). A future
 * implementation would use a software depth buffer or Hi-Z and process in
 * front-to-back order.
 */
namespace OcclusionCuller {

/**
 * @brief True if the AABB is not fully occluded (currently always true; TODO).
 * @param boundsMin World-space AABB minimum.
 * @param boundsMax World-space AABB maximum.
 * @param context VisibilityContext.
 */
inline bool isVisible(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const VisibilityContext& context
) {
    // TODO
    return true;
}

} // namespace OcclusionCuller

} // namespace Engine
