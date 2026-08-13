#pragma once

#include <glm/glm.hpp>

#include "core/math/frustum.h"

namespace Engine {


/**
 * @brief Per-frame data for visibility and culling (frustum, view matrix, camera position, thresholds).
 */
struct VisibilityContext {
    Math::Frustum frustum;       ///< View frustum planes (from Math::extractFrustum).
    glm::vec3 cameraPosition;    ///< Camera position in world space.
    glm::mat4 view;              ///< View matrix (world -> view space).

    float minPixels;             ///< Min projected size in pixels; below this, screen-size culling rejects. <= 0 disables.
    float maxDistance;           ///< Max distance from camera; beyond this, distance culling rejects. <= 0 disables.
    float maxDistanceSquared;    ///< Pre-computed maxDistance^2 for squared-distance comparisons.
    float screenSizeThresholdSq; ///< Pre-computed (minPixels / (projScaleY * viewportHeight))^2 for sqrt-free screen-size test.

};

} // namespace Engine
