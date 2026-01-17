#include "visibility.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

#include "logger.h"

#include "frustum_culler.h"
#include "mesh_asset.h"
#include "transform.h"
#include "camera.h"
#include "scene.h"
#include "resource_manager.h"

namespace Engine {

bool hasValidBounds(const glm::vec3& min, const glm::vec3& max) {
    constexpr float epsilon = 1e-6f;
    return (std::abs(max.x - min.x) > epsilon) || 
           (std::abs(max.y - min.y) > epsilon) || 
           (std::abs(max.z - min.z) > epsilon);
}

bool shouldCullMesh(
    const MeshAsset& meshAsset,
    const glm::mat4& modelMatrix,
    const Frustum& frustum
) {
    // Skip culling if mesh has no valid bounds
    if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) {
        return false;  // Can't cull without bounds, so keep it
    }

    // Transform AABB from model space to world space
    glm::vec3 worldMin, worldMax;
    FrustumCuller::transformAABB(
        modelMatrix,
        meshAsset.boundsMin,
        meshAsset.boundsMax,
        worldMin,
        worldMax
    );

    // Cull if AABB is not visible
    return !FrustumCuller::isAABBVisible(frustum, worldMin, worldMax);
}

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

    const Frustum frustum = FrustumCuller::extractFrustum(projectionViewMatrix);

    const auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    result.entities.reserve(meshStorage.size());

    for (EntityId id = 0; id < meshStorage.size(); ++id) {
        if (!meshStorage.has(id)) continue;

        const auto& mesh = meshStorage.get(id);

        if (!mesh.visible) continue;

        if (!transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);
        const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);

        const auto& meshAsset = resources.get(mesh.mesh);

        if (shouldCullMesh(meshAsset, modelMatrix, frustum)) {
            continue;
        }

        result.entities.push_back(id);
    }

    // Sort visible entities by id for faster access in the scene storages
    std::sort(result.entities.begin(), result.entities.end());

    return result;
}

} // namespace Engine
