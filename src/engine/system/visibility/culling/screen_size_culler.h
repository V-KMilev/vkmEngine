#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "system/visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Screen-size culling: reject AABBs that project to fewer than minPixels.
 *
 * Estimates 2D size from the world AABB bounding sphere; objects behind the
 * camera or with zero viewport are kept.
 */
namespace ScreenSizeCuller {

/**
 * @brief True if the projected size is >= context.minPixels (or behind camera).
 *
 * Uses squared comparison to avoid per-entity sqrt:
 *   worldRadiusSq / depthSq >= screenSizeThresholdSq
 * where screenSizeThresholdSq = (minPixels / (projScaleY * viewportHeight))^2
 * is pre-computed once per frame in VisibilityContext.
 *
 * @param boundsMin World-space AABB minimum.
 * @param boundsMax World-space AABB maximum.
 * @param context VisibilityContext with view, screenSizeThresholdSq.
 */
inline bool isVisible(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const VisibilityContext& context
) {
    if (context.minPixels <= 0.0f) {
        return true;
    }

    const glm::vec3 worldCenter = (boundsMin + boundsMax) * 0.5f;
    const glm::vec3 worldHalfExtent = (boundsMax - boundsMin) * 0.5f;
    const float worldRadiusSq = glm::dot(worldHalfExtent, worldHalfExtent);

    const glm::vec3 viewCenter = glm::vec3(context.view * glm::vec4(worldCenter, 1.0f));
    const float depth = -viewCenter.z;

    if (depth <= glm::epsilon<float>()) {
        return true;
    }

    // worldRadiusSq / depthSq >= thresholdSq  (both sides positive, sqrt-free)
    return worldRadiusSq >= context.screenSizeThresholdSq * (depth * depth);
}

} // namespace ScreenSizeCuller

} // namespace Engine
