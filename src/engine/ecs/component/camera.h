#pragma once

#include <glm/glm.hpp>

#include "core/math/projection.h"
#include "core/reflect.h"

namespace Engine {

/**
 * @brief Enumeration of camera projection types.
 */
enum class ProjectionType {
    Perspective  = 0,   ///< Perspective projection
    Orthographic = 1,   ///< Orthographic (parallel) projection
    Count               ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

VKM_ENUM_NAMES(ProjectionType, "Perspective", "Orthographic")

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
    float aspect              = 0.0f;                           ///< Aspect ratio (width / height); <= 0 = derive from the viewport each frame
    float zNear               = 0.1f;                           ///< Near clip plane distance
    float zFar                = 1000.0f;                        ///< Far clip plane distance
    float exposure            = 1.0f;                           ///< Camera exposure (linear multiplier for final output)
    float focusDistance       = 10.0f;                          ///< Depth of field: world distance held in sharp focus
    float dofAmount           = 0.0f;                           ///< Depth of field strength (0 = off)
    bool active               = true;                           ///< Is this camera active?

    /**
     * @brief Compute the projection matrix for this camera.
     *
     * @param camera         The camera to build the projection for.
     * @param viewportAspect The aspect used while camera.aspect <= 0 (auto mode:
     *                       the camera tracks the viewport it renders into).
     * @return The projection matrix.
     */
    static glm::mat4 computeProjection(const Camera& camera, float viewportAspect) {
        const float aspect = camera.aspect > 0.0f ? camera.aspect : viewportAspect;
        if (camera.projection == ProjectionType::Perspective) {
            return Math::makePerspective(camera.fovY, aspect, camera.zNear, camera.zFar);
        } else {
            return Math::makeOrthographic(camera.orthoHeight, aspect, camera.zNear, camera.zFar);
        }
    }
};

VKM_REFLECT_BEGIN(Camera)
    VKM_F(projection),
    VKM_F(fovY),
    VKM_F(orthoHeight),
    VKM_F(aspect),
    VKM_F(zNear),
    VKM_F(zFar),
    VKM_F(exposure),
    VKM_F(focusDistance),
    VKM_F(dofAmount),
    VKM_F(active)
VKM_REFLECT_END()

} // namespace Engine
