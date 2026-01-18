#include "render_view.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "scene.h"
#include "visibility.h"
#include "transform.h"
#include "camera.h"
#include "mesh.h"
#include "light.h"
#include "resource_manager.h"

namespace Engine {

namespace {
    /**
     * @brief Helper function to setup camera data in the render view.
     * @return true if camera was found and setup, false otherwise.
     */
    bool setupCamera(CameraData& cameraData, const Scene& scene) {
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        // Find the active camera
        for (EntityId id = 0; id < cameraStorage.size(); ++id) {
            if (!cameraStorage.has(id)) continue;

            const auto& camera = cameraStorage.get(id);
            if (!camera.active) continue;

            if (!transformStorage.has(id)) continue;

            const auto& transform = transformStorage.get(id);

            // Compute camera matrices
            cameraData.position = transform.position;
            const glm::vec3 forward = Transform::computeForward(transform.rotation);
            const glm::vec3 up = Transform::computeUp(transform.rotation);

            cameraData.view = Transform::computeView(transform);
            cameraData.projection = Camera::computeProjection(camera);
            cameraData.viewProjection = cameraData.projection * cameraData.view;

            return true;
        }

        return false;
    }
}

RenderView RenderView::build(
    const Scene& scene,
    const ResourceManager& resources,
    const Visibility& visibility
) {
    RenderView renderView;

    if (!setupCamera(renderView.camera, scene)) {
        LOG_ERROR("No active camera found");
        return renderView;
    }

    const auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    // Gather drawables
    renderView.drawables.reserve(visibility.entities.size());

    // Iterate through entities and matrices in parallel (same order, cache-friendly)
    for (auto id : visibility.entities) {
        if (!meshStorage.has(id)) continue;
        if (!transformStorage.has(id)) continue;

        const auto& mesh = meshStorage.get(id);
        const auto& transform = transformStorage.get(id);

        // Use cached model matrix from visibility (computed during culling)
        // Matrices are stored in same order as entities for cache-friendly access
        // Add drawable with cached matrix
        DrawableData drawable;
        drawable.mesh     = mesh.mesh;
        drawable.material = mesh.material;
        drawable.model    = Transform::computeModelMatrix(transform);
        renderView.drawables.emplace_back(drawable);
    }

    // Gather lights
    const auto& lightStorage = scene.storage<Light>();
    renderView.lights.reserve(lightStorage.size());

    for (EntityId id = 0; id < lightStorage.size(); ++id) {
        if (!lightStorage.has(id)) continue;

        const auto& light = lightStorage.get(id);

        if (!light.enabled) continue;

        if (!transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);

        // Add light to render view (copy component data + transform)
        LightData lightData;
        lightData.type = light.type;
        lightData.color = light.color;
        lightData.intensity = light.intensity;
        lightData.radius = light.radius;
        lightData.innerConeAngle = light.innerConeAngle;
        lightData.outerConeAngle = light.outerConeAngle;
        lightData.castShadows = light.castShadows;
        lightData.position = transform.position;
        lightData.rotation = transform.rotation;

        renderView.lights.emplace_back(lightData);
    }

    return renderView;
}

} // namespace Engine
