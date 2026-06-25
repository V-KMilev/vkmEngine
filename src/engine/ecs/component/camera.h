#pragma once

#include <glm/glm.hpp>

#include "core/math/projection.h"

namespace Engine {

/**
 * @brief Enumeration of camera projection types.
 */
enum class ProjectionType {
    Perspective  = 0,   ///< Perspective projection
    Orthographic = 1    ///< Orthographic (parallel) projection
};

/**
 * @brief Names in ProjectionType order - the single source for JSON
 * (de)serialization and editor combos, so the two cannot drift.
 */
inline constexpr const char* const PROJECTION_TYPE_NAMES[] = {
    "Perspective", "Orthographic"
};

/**
 * @brief Component representing a camera, containing projection and view parameters.
 *
 * Simple data-only component. Supports both perspective and orthographic projection.
 * Camera calculations (view/projection matrices) should be handled by systems that process this component.
 *
 * For raw projection-matrix construction without a Camera instance, use the
 * builders in core/math/projection.h.
 */
struct Camera {
    ProjectionType projection = ProjectionType::Perspective;    ///< Type of projection
    float fovY                = glm::radians(70.0f);            ///< Vertical field of view in radians (default: ~70 degrees)
    float orthoHeight         = 10.0f;                          ///< Orthographic half-height in world units
    float aspect              = 16.0f / 9.0f;                   ///< Aspect ratio (width / height)
    float zNear               = 0.1f;                           ///< Near clip plane distance
    float zFar                = 1000.0f;                        ///< Far clip plane distance
    float exposure            = 1.0f;                           ///< Camera exposure (linear multiplier for final output)
    bool active               = true;                           ///< Is this camera active?

    /**
     * @brief Compute the projection matrix for this camera.
     */
    static glm::mat4 computeProjection(const Camera& camera) {
        if (camera.projection == ProjectionType::Perspective) {
            return Math::makePerspective(camera.fovY, camera.aspect, camera.zNear, camera.zFar);
        } else {
            return Math::makeOrthographic(camera.orthoHeight, camera.aspect, camera.zNear, camera.zFar);
        }
    }
};

} // namespace Engine
