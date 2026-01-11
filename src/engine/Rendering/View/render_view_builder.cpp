#include "render_view_builder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "scene.h"
#include "transform.h"
#include "camera.h"
#include "mesh.h"
#include "resource_manager.h"
#include "frustum_culler.h"

namespace Engine {

namespace {
    /**
     * @brief Helper function to check if an AABB has valid (non-zero) volume.
     */
    bool hasValidBounds(const glm::vec3& min, const glm::vec3& max) {
        return (min.x != max.x) || (min.y != max.y) || (min.z != max.z);
    }

    /**
     * @brief Helper function to perform frustum culling test for a mesh.
     * @return true if the mesh should be culled (not visible), false if visible.
     */
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

    /**
     * @brief Helper function to setup camera data in the render view.
     * @return true if camera was found and setup, false otherwise.
     */
    bool setupCamera(CameraData& cameraData, const Scene& scene) {
        const auto& cameraStorage = scene.storage<Camera>();
        const auto& transformStorage = scene.storage<Transform>();

        // Find the active camera
        for (EntityId id = 0; id < cameraStorage.size(); ++id) {
            if (!cameraStorage.has(id) || !cameraStorage.get(id).active) {
                continue;
            }

            if (!transformStorage.has(id)) {
                continue;
            }

            const auto& camera = cameraStorage.get(id);
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

RenderView RenderViewBuilder::build(const Scene& scene, const ResourceManager& resources) {
    RenderView renderView;

    // Setup camera
    if (!setupCamera(renderView.camera, scene)) {
        LOG_ERROR("No active camera found");
        return renderView;
    }

    // Extract frustum for culling
    const Frustum frustum = FrustumCuller::extractFrustum(renderView.camera.viewProjection);

    // Gather drawables
    const auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    renderView.drawables.reserve(meshStorage.size());

    for (EntityId id = 0; id < meshStorage.size(); ++id) {
        // Skip if entity doesn't have mesh component
        if (!meshStorage.has(id)) {
            continue;
        }

        const auto& mesh = meshStorage.get(id);
        if (!mesh.visible) {
            continue;
        }

        // Skip if entity doesn't have transform component
        if (!transformStorage.has(id)) {
            continue;
        }

        const auto& transform = transformStorage.get(id);
        const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);

        // Perform frustum culling
        const auto& meshAsset = resources.get(mesh.mesh);
        if (shouldCullMesh(meshAsset, modelMatrix, frustum)) {
            continue;
        }

        // Add drawable to render view
        DrawableData drawable;
        drawable.mesh     = mesh.mesh;
        drawable.material = mesh.material;
        drawable.model    = modelMatrix;

        renderView.drawables.emplace_back(drawable);
    }

    return renderView;
}

} // namespace Engine