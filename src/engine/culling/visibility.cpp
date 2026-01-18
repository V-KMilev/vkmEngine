#include "visibility.h"

#include "scene.h"
#include "camera.h"
#include "transform.h"

#include "frustum_culler.h"
#include "spatial_index.h"

namespace Engine {

static SpatialIndex m_spatialIndex;

// TODO: Think of way to cache the view, projection and view projection matrices so
//  we don't recompute them every time we need them
static glm::mat4 computeViewProjection(const Scene& scene) {
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

Visibility buildVisibility(
    const Scene& scene,
    const ResourceManager& resources
) {
    Visibility visibility;

    const glm::mat4 viewProjection = computeViewProjection(scene);

    // Update spatial index (rebuilds BVH if needed)
    m_spatialIndex.update(scene, resources);

    // Extract frustum for culling
    const Frustum frustum = FrustumCuller::extractFrustum(viewProjection);

    // Query visible entities from octree
    visibility.entities = m_spatialIndex.queryVisible(frustum);

    // std::sort(visibleIds.begin(), visibleIds.end());

    return visibility;
}

} // namespace Engine