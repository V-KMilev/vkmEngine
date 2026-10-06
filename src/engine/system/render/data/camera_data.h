#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Flattened camera for the frame.
 */
struct CameraData {
    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);
    glm::mat4 invProjection  = glm::mat4(1.0f);
    glm::mat4 invView        = glm::mat4(1.0f);
    glm::mat4 invViewProj    = glm::mat4(1.0f);
    glm::vec3 position       = glm::vec3(0.0f);

    float zNear = 0.1f;    ///< Extracted from the projection.
    float zFar  = 1000.0f; ///< Extracted from the projection.

    // Depth of field, as Camera states it; zero (captures, previews) draws sharp.
    float focusDistance = 0.0f;
    float dofAmount     = 0.0f;
    float dofMaxBlur    = 0.0f;

    /**
     * @brief A camera with every derived field filled from its two matrices.
     *
     * Build a camera here, not field by field, so no derivative is stale.
     *
     * @param view       World to view.
     * @param projection View to clip, perspective or orthographic.
     * @param position   The eye in world space.
     * @return The camera, with no depth of field.
     */
    static CameraData from(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& position) {
        CameraData camera;
        camera.view           = view;
        camera.projection     = projection;
        camera.position       = position;
        camera.viewProjection = projection * view;
        camera.invProjection  = glm::inverse(projection);
        camera.invView        = glm::inverse(view);
        camera.invViewProj    = glm::inverse(camera.viewProjection);

        // Perspective and ortho need different identities: the perspective form on
        // an ortho matrix yields a negative zFar, which shaders/depth.glsl takes a log of.
        if (projection[3][3] == 0.0f) {
            camera.zNear = projection[3][2] / (projection[2][2] - 1.0f);
            camera.zFar  = projection[3][2] / (projection[2][2] + 1.0f);
        } else {
            camera.zNear = (projection[3][2] + 1.0f) / projection[2][2];
            camera.zFar  = (projection[3][2] - 1.0f) / projection[2][2];
        }
        return camera;
    }
};

} // namespace Vkm::Engine
