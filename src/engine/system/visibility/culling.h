#pragma once

#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "base/math/aabb.h"
#include "base/math/frustum.h"

namespace Vkm::Engine {

/**
 * @brief Per-frame data the culls read.
 */
struct VisibilityContext {
    Math::Frustum frustum;
    glm::vec3     cameraPosition;  ///< World space.
    glm::mat4     view;

    float screenSizeThresholdSq;    ///< (MIN_PIXELS / (projScaleY * viewportHeight))^2.
    float lodDistanceScale = 1.0f;  ///< LOD::REFERENCE_P11 over the view's projection[1][1]: what a distance
                                    ///< counts for in LOD selection.

    bool perspective = true;  ///< Whether the view divides by depth. False turns screen-size culling off:
                              ///< orthographic size does not fall with distance.
};

/**
 * @brief The cull past the frustum test, which is Math::frustumIntersectsAABB.
 */
namespace Culling {

constexpr float MIN_PIXELS = 3.0f;  ///< The screen-pixel size an object is culled under, faded out above it
                                    ///< (FADE_BAND).

constexpr float FADE_BAND = 0.075f;  ///< The band an object fades across, as a share of where it starts
                                     ///< (RenderObjects::fades): past a level's range, the next level fading
                                     ///< in; under this much over MIN_PIXELS, the object fading out.

/**
 * @brief The size the AABB's bounding sphere projects to, over MIN_PIXELS.
 *
 * Infinite where screen-size culling does not apply: behind the camera, under an orthographic
 * view, or with a zero viewport.
 *
 * @param bounds  World-space AABB.
 * @param context Supplies the view and screenSizeThresholdSq.
 * @return How many times MIN_PIXELS the box spans.
 */
inline float sizeOverCull(const Math::AABB& bounds, const VisibilityContext& context) {
    constexpr float UNCULLED = std::numeric_limits<float>::infinity();
    if (!context.perspective) return UNCULLED;

    const glm::vec3 worldHalfExtent = bounds.halfExtent();
    const float worldRadiusSq = glm::dot(worldHalfExtent, worldHalfExtent);

    const glm::vec3 viewCenter = glm::vec3(context.view * glm::vec4(bounds.center(), 1.0f));
    const float depth   = -viewCenter.z;
    const float limitSq = context.screenSizeThresholdSq * (depth * depth);
    if (depth <= glm::epsilon<float>() || limitSq <= 0.0f) return UNCULLED;

    return std::sqrt(worldRadiusSq / limitSq);
}

/**
 * @brief Screen-size culling: whether the AABB's bounding sphere projects to MIN_PIXELS or more.
 *
 * @param bounds  World-space AABB.
 * @param context Supplies the view and screenSizeThresholdSq.
 * @return Whether the box survives; always where sizeOverCull says the test does not apply.
 */
inline bool isLargeEnough(const Math::AABB& bounds, const VisibilityContext& context) {
    return sizeOverCull(bounds, context) >= 1.0f;
}

} // namespace Culling

} // namespace Vkm::Engine
