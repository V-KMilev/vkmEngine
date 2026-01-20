#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "mesh.h"
#include "visibility_context.h"

namespace Engine {

/**
 * @brief Screen-size culling: reject meshes that project to fewer than minPixels.
 *
 * Estimates 2D size from the world AABB’s bounding sphere; objects behind the
 * camera or with zero viewport are kept.
 */
namespace ScreenSizeCuller {

/**
 * @brief True if the mesh’s projected size is >= context.minPixels (or viewport is zero / behind camera).
 * @param mesh Mesh with world boundsMin/boundsMax.
 * @param context VisibilityContext with view, projection, viewport, minPixels.
 */
inline bool isVisible(
    const Mesh& mesh,
    const VisibilityContext& context
) {
    if (context.viewportWidth == 0 || context.viewportHeight == 0) {
        return true;
    }

    const glm::vec3 worldCenter = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
    const glm::vec3 worldHalfExtent = (mesh.boundsMax - mesh.boundsMin) * 0.5f;
    const float worldRadius = glm::length(worldHalfExtent);

    const glm::vec3 viewCenter = glm::vec3(context.view * glm::vec4(worldCenter, 1.0f));
    const float depth = -viewCenter.z;

    if (depth <= glm::epsilon<float>()) {
        return true;
    }

    const float projScaleY = context.projection[1][1];
    const float projectedRadiusNdc = (worldRadius * projScaleY) / depth;
    const float projectedPixels = projectedRadiusNdc * static_cast<float>(context.viewportHeight);

    return projectedPixels >= context.minPixels;
}

} // namespace ScreenSizeCuller

} // namespace Engine
