#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Flattened camera for the frame.
 *
 * Filled once from the active camera so the backend never searches the scene.
 */
struct CameraData {
    glm::mat4 view;            ///< The view matrix for the camera.
    glm::mat4 projection;      ///< The projection matrix for the camera.
    glm::mat4 viewProjection;  ///< projection * view, precomputed once.
    glm::mat4 invProjection;   ///< inverse(projection), precomputed once.
    glm::mat4 invView;         ///< inverse(view), precomputed once.
    glm::mat4 invViewProj;     ///< inverse(projection * view), precomputed once.
    glm::vec3 position;        ///< The position of the camera in world space.

    float zNear = 0.1f;    ///< Near plane, extracted from the projection.
    float zFar  = 1000.0f; ///< Far plane, extracted from the projection.

    float focusDistance = 10.0f;  ///< Depth of field: world distance held in sharp focus.
    float dofAmount     = 0.0f;   ///< Depth of field strength (0 = off).

    /**
     * @brief Fill every derived field from view + projection.
     *
     * viewProjection, the three inverses, and the two-coefficient zNear/zFar
     * extraction. Every producer (RenderView, the preview renderer, the probe
     * and irradiance bakers) calls this after setting the two matrices, so no
     * consumer can ever read a stale or uninitialized derivative.
     */
    void derive() {
        viewProjection = projection * view;
        invProjection  = glm::inverse(projection);
        invView        = glm::inverse(view);
        invViewProj    = glm::inverse(viewProjection);
        zNear = projection[3][2] / (projection[2][2] - 1.0f);
        zFar  = projection[3][2] / (projection[2][2] + 1.0f);
    }
};

} // namespace Engine
