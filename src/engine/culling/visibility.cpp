#include "visibility.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

#include "logger.h"

#include "scene.h"
#include "camera.h"
#include "transform.h"

#include "frustum_culler.h"
#include "spatial_index.h"

namespace Engine {

static SpatialIndex m_spatialIndex;

// TODO: Think of way to cache the view, projection and view projection matrices so
//  we don't recompute them every time we need them
bool computeProjectionViewMatrix(glm::mat4& projectionViewMatrix, const Scene& scene) {
    const auto& cameraStorage = scene.storage<Camera>();
    const auto& transformStorage = scene.storage<Transform>();

    // Find the active camera
    for (EntityId id = 0; id < cameraStorage.size(); ++id) {
        if (!cameraStorage.has(id)) continue;

        const auto& camera = cameraStorage.get(id);

        if (!camera.active) continue;

        if (!transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);

        projectionViewMatrix = Camera::computeProjection(camera) * Transform::computeView(transform);

        return true;
    }

    return false;
}

Visibility buildVisibility(
    const Scene& scene,
    const ResourceManager& resources
) {
    Visibility result;

    glm::mat4 projectionViewMatrix;

    if (!computeProjectionViewMatrix(projectionViewMatrix, scene)) {
        LOG_WARNING("No active camera found, skipping visibility filtering");
        return result;
    }

    // Update spatial index (rebuilds BVH if needed)
    m_spatialIndex.update(scene, resources);

    // Extract frustum for culling
    const Frustum frustum = FrustumCuller::extractFrustum(projectionViewMatrix);

    // Query visible entities from octree
    result.entities = m_spatialIndex.queryVisible(frustum);

    return result;
}

} // namespace Engine
