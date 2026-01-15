#include "scene_view.h"

namespace Engine {

SceneView::SceneView()
    : m_spatialIndex()
{
}

std::vector<uint32_t> SceneView::getVisibleEntities(
    const Scene& scene,
    const ResourceManager& resources
) {
    std::vector<uint32_t> visibleIds = {};

    const glm::mat4 viewProjection = computeViewProjection(scene);

    // Update spatial index (rebuilds BVH if needed)
    m_spatialIndex.update(scene, resources);

    // Extract frustum for culling
    const Frustum frustum = FrustumCuller::extractFrustum(viewProjection);

    // Query visible entities from octree
    visibleIds = m_spatialIndex.queryVisible(frustum);

    // std::sort(visibleIds.begin(), visibleIds.end());

    return visibleIds;
}

// TODO: Think of way to cache the view, projection and view projection matrices so
//  we don't recompute them every time we need them
glm::mat4 SceneView::computeViewProjection(const Scene& scene) const {
    const auto& cameraStorage = scene.storage<Camera>();
    const auto& transformStorage = scene.storage<Transform>();

    // Find the active camera using dense iteration
    for (EntityId id : cameraStorage.entities()) {
        const auto& camera = cameraStorage.get(id);

        if (!camera.active) continue;
        if (!transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);

        // Compute camera matrices
        const glm::vec3 forward = Transform::computeForward(transform.rotation);
        const glm::vec3 up = Transform::computeUp(transform.rotation);

        glm::mat4 view = glm::lookAt(
            transform.position,
            transform.position + forward,
            up
        );

        glm::mat4 projection = Camera::computeProjection(camera);
        glm::mat4 viewProjection = projection * view;

        return viewProjection;
    }

    return glm::mat4(1.0f);
}


} // namespace Engine