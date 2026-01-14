#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

/**
 * @brief Enumeration of camera projection types.
 */
enum class ProjectionType {
    Perspective  = 0,   ///< Perspective projection
    Orthographic = 1    ///< Orthographic (parallel) projection
};

/**
 * @brief Component representing a camera, containing projection and view parameters.
 *
 * Simple data-only component. Supports both perspective and orthographic projection.
 * Camera calculations (view/projection matrices) should be handled by systems that process this component.
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
     * @brief Compute a perspective projection matrix.
     * @param fovY Vertical field of view in radians.
     * @param aspect Aspect ratio (width / height).
     * @param zNear Near clip plane distance.
     * @param zFar Far clip plane distance.
     * @return The perspective projection matrix.
     */
    static glm::mat4 makePerspective(float fovY, float aspect, float zNear, float zFar) {
        return glm::perspective(fovY, aspect, zNear, zFar);
    }

    /**
     * @brief Compute an orthographic projection matrix.
     * @param halfHeight Orthographic half-height in world units.
     * @param aspect Aspect ratio (width / height).
     * @param zNear Near clip plane distance.
     * @param zFar Far clip plane distance.
     * @return The orthographic projection matrix.
     */
    static glm::mat4 makeOrthographic(float halfHeight, float aspect, float zNear, float zFar) {
        const float halfWidth = halfHeight * aspect;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, zNear, zFar);
    }

    /**
     * @brief Compute the projection matrix for this camera.
     * @param camera The camera component.
     * @return The computed projection matrix.
     */
    static glm::mat4 computeProjection(const Camera& camera) {
        if (camera.projection == ProjectionType::Perspective) {
            return makePerspective(camera.fovY, camera.aspect, camera.zNear, camera.zFar);
        } else {
            return makeOrthographic(camera.orthoHeight, camera.aspect, camera.zNear, camera.zFar);
        }
    }
};

} // namespace Engine
