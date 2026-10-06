#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "core/math/bounds.h"
#include "core/math/frustum.h"

namespace Vkm::Engine {

/**
 * @brief Per-frame data the culls read.
 */
struct VisibilityContext {
    Math::Frustum frustum;
    glm::vec3     cameraPosition;  ///< World space.
    glm::mat4     view;

    float minPixels;              ///< Pixels below which screen-size culling rejects; <= 0 disables.
    float maxDistance;            ///< Distance beyond which distance culling rejects; <= 0 disables.
    float maxDistanceSquared;
    float screenSizeThresholdSq;  ///< (minPixels / (projScaleY * viewportHeight))^2.
    /// LOD::REFERENCE_P11 over the view's projection[1][1]: what a distance counts for in LOD selection.
    float lodDistanceScale = 1.0f;

    /**
     * @brief Whether the view divides by depth.
     *
     * False turns screen-size culling off: orthographic size does not fall with distance.
     */
    bool perspective = true;
};

/**
 * @brief The culls past the frustum test, which is Math::frustumIntersectsAABB.
 */
namespace Culling {

/**
 * @brief Distance culling: whether the AABB's centre is within maxDistance of the camera.
 *
 * @param bounds  World-space AABB.
 * @param context Supplies cameraPosition and maxDistance (0 or below disables).
 * @return Whether the box survives.
 */
inline bool isNearEnough(const Math::AABB& bounds, const VisibilityContext& context) {
    if (context.maxDistance <= 0.0f) return true;

    const glm::vec3 delta = bounds.center() - context.cameraPosition;
    return glm::dot(delta, delta) <= context.maxDistanceSquared;
}

/**
 * @brief Screen-size culling: whether the AABB's bounding sphere projects to minPixels or more.
 *
 * The test does not apply behind the camera, under an orthographic view, with a zero
 * viewport, or with minPixels at 0 or below.
 *
 * @param bounds  World-space AABB.
 * @param context Supplies the view and screenSizeThresholdSq.
 * @return Whether the box survives.
 */
inline bool isLargeEnough(const Math::AABB& bounds, const VisibilityContext& context) {
    if (context.minPixels <= 0.0f || !context.perspective) return true;

    const glm::vec3 worldHalfExtent = bounds.halfExtent();
    const float     worldRadiusSq   = glm::dot(worldHalfExtent, worldHalfExtent);

    const glm::vec3 viewCenter = glm::vec3(context.view * glm::vec4(bounds.center(), 1.0f));
    const float     depth      = -viewCenter.z;
    if (depth <= glm::epsilon<float>()) return true;

    return worldRadiusSq >= context.screenSizeThresholdSq * (depth * depth);
}

} // namespace Culling

} // namespace Vkm::Engine
