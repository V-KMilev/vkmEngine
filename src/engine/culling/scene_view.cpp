#include "scene_view.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "scene.h"
#include "camera.h"
#include "transform.h"
#include "frustum_culler.h"
#include "resource_manager.h"
#include "spatial_index.h"

namespace Engine {

namespace {

    /**
     * @brief Compute view-projection matrix from the active camera in the scene.
     * @return View-projection matrix, or identity if no active camera found.
     */
    glm::mat4 computeViewProjection(const Scene& scene) {
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        for (EntityId id = 0; id < cameraStorage.size(); ++id) {
            if (!cameraStorage.has(id)) continue;

            const auto& camera = cameraStorage.get(id);
            if (!camera.active) continue;
            if (!transformStorage.has(id)) continue;

            const auto& transform = transformStorage.get(id);

            // Compute camera matrices
            const glm::vec3 forward = Transform::computeForward(transform.rotation);
            const glm::vec3 up = Transform::computeUp(transform.rotation);

            const glm::mat4 view = glm::lookAt(
                transform.position,
                transform.position + forward,
                up
            );

            const glm::mat4 projection = Camera::computeProjection(camera);
            return projection * view;
        }

        return glm::mat4(1.0f);
    }

} // anonymous namespace

SceneView::SceneView()
    : m_spatialIndex()
{}

std::vector<EntityId> SceneView::getVisibleEntities(
    const Scene& scene,
    const ResourceManager& resources
) {
    // Compute view-projection matrix from active camera
    const glm::mat4 viewProjection = computeViewProjection(scene);

    // Update spatial index (rebuilds BVH if needed)
    m_spatialIndex.update(scene, resources);

    // Extract frustum for culling
    const Frustum frustum = FrustumCuller::extractFrustum(viewProjection);

    // Get visible entities from BVH
    std::vector<EntityId> visibleIds = m_spatialIndex.getVisible(frustum);

    // Sort for consistent ordering (faster iteration in the scene storages)
    std::sort(visibleIds.begin(), visibleIds.end());

    return visibleIds;
}

} // namespace Engine
