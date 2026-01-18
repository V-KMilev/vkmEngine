#include "render_view.h"

#include <algorithm>
#include <future>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"
#include "thread_pool.h"

#include "scene.h"
#include "transform.h"
#include "camera.h"
#include "mesh.h"
#include "light.h"
#include "resource_manager.h"
#include "visibility.h"

namespace Engine {

namespace {
    /**
     * @brief Helper function to setup camera data in the render view.
     * @return true if camera was found and setup, false otherwise.
     */
    bool setupCamera(CameraData& cameraData, const Scene& scene) {
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        // Find the active camera using dense iteration
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

            cameraData.view = glm::lookAt(
                cameraData.position,
                cameraData.position + forward,
                up
            );
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
    const glm::vec3 cameraPos = renderView.camera.position;

    // Screen-size culling: skip objects smaller than ~2 pixels at 1080p
    constexpr float MIN_SCREEN_SIZE_SQ = 0.002f * 0.002f;

    auto buildDrawable = [&](uint32_t id, std::vector<DrawableData>& out) {
        if (!meshStorage.has(id) || !transformStorage.has(id)) return;

        const auto& mesh = meshStorage.get(id);
        if (!mesh.visible) return;

        const auto& transform = transformStorage.get(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        const glm::vec3 diff = transform.position - cameraPos;
        const float distSq = glm::dot(diff, diff);

        // Estimate screen size from mesh bounds
        const glm::vec3 extents = (meshAsset.boundsMax - meshAsset.boundsMin) * 0.5f;
        const float maxScale = std::max({transform.scale.x, transform.scale.y, transform.scale.z});
        const float radius = std::max({extents.x, extents.y, extents.z}) * maxScale;

        if (distSq > 1.0f && (radius * radius) / distSq < MIN_SCREEN_SIZE_SQ) {
            return;
        }

        out.push_back(DrawableData{
            mesh.mesh,
            mesh.material,
            Transform::computeModelMatrix(transform)
        });
    };

    renderView.drawables.reserve(visibility.entities.size());
    for (EntityId id : visibility.entities) {
        buildDrawable(id, renderView.drawables);
    }

    // Sort by material to minimize state changes
    std::sort(renderView.drawables.begin(), renderView.drawables.end(),
        [](const DrawableData& a, const DrawableData& b) {
            return a.material.value < b.material.value;
        });

    // Gather lights
    const auto& lightStorage = scene.storage<Light>();
    renderView.lights.reserve(lightStorage.size());

    for (EntityId id = 0; id < lightStorage.size(); ++id) {
        if (!lightStorage.has(id)) continue;

        const auto& light = lightStorage.get(id);
        if (!light.enabled || !transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);
        renderView.lights.push_back({
            light.type, light.color, light.intensity, light.radius,
            light.innerConeAngle, light.outerConeAngle, light.castShadows,
            transform.position, transform.rotation
        });
    }

    return renderView;
}

} // namespace Engine