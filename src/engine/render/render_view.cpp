#include "render/render_view.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "visibility/visibility.h"
#include "ecs/component/transform.h"
#include "ecs/component/camera.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {

    /**
     * @brief Sort drawables by (material, mesh) for optimal batching.
     *
     * Uses std::sort with composite key - faster than radix sort when
     * handle values are sparse (large gaps between values).
     */
    void sortDrawables(std::vector<DrawableData>& drawables) {
        if (drawables.size() <= 1) return;

        std::sort(drawables.begin(), drawables.end(),
            [](const DrawableData& a, const DrawableData& b) {
                // Primary: material, Secondary: mesh
                if (a.material.id() != b.material.id()) {
                    return a.material.id() < b.material.id();
                }
                return a.mesh.id() < b.mesh.id();
            });
    }

    /**
     * @brief Helper function to setup camera data in the render view.
     * @return true if camera was found and setup, false otherwise.
     */
    bool setupCamera(CameraData& cameraData, const Scene& scene) {
        bool found = false;
        scene.forEach<Camera, Transform>([&](EntityId id, const Camera& camera, const Transform& transform) {
            if (found) return;
            if (!camera.active) return;

            cameraData.position = transform.position;

            cameraData.view = Transform::computeView(transform);
            cameraData.projection = Camera::computeProjection(camera);
            cameraData.viewProjection = cameraData.projection * cameraData.view;

            found = true;
        });

        return found;
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

    // Gather drawables
    renderView.drawables.reserve(visibility.entities.size());

    // Iterate through entities and matrices in parallel (same order, cache-friendly)
    for (size_t i = 0; i < visibility.entities.size(); ++i) {
        EntityId id = visibility.entities[i];
        if (!scene.has<Mesh>(id)) continue;

        const auto& mesh = scene.get<Mesh>(id);

        // Use cached model matrix from visibility (computed during culling)
        // Matrices are stored in same order as entities for cache-friendly access
        // Add drawable with cached matrix
        DrawableData drawable;
        drawable.mesh     = mesh.mesh;
        drawable.material = mesh.material;
        drawable.model    = visibility.modelMatrices[i];
        renderView.drawables.emplace_back(drawable);
    }

    // Sort drawables by for optimal batching
    sortDrawables(renderView.drawables);

    // Gather lights (dense iteration, no holes)
    renderView.lights.reserve(scene.count<Light>());

    scene.forEach<Light, Transform>([&](EntityId id, const Light& light, const Transform& transform) {
        if (!light.enabled) return;

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
    });

    return renderView;
}

} // namespace Engine
