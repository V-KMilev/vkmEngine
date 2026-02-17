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
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        bool found = false;
        cameraStorage.forEach([&](EntityId id, const Camera& camera) {
            if (found) return;
            if (!camera.active) return;
            if (!transformStorage.has(id)) return;

            const auto& transform = transformStorage.get(id);

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

    const auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    // Gather drawables
    renderView.drawables.reserve(visibility.entities.size());

    // Iterate through entities and matrices in parallel (same order, cache-friendly)
    for (size_t i = 0; i < visibility.entities.size(); ++i) {
        EntityId id = visibility.entities[i];
        if (!meshStorage.has(id)) continue;

        const auto& mesh = meshStorage.get(id);

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
    const auto& lightStorage = scene.storage<Light>();
    renderView.lights.reserve(lightStorage.count());

    lightStorage.forEach([&](EntityId id, const Light& light) {
        if (!light.enabled) return;
        if (!transformStorage.has(id)) return;

        const auto& transform = transformStorage.get(id);

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
