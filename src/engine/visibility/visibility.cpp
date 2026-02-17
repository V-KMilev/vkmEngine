#include "visibility.h"

#include "logger.h"

#include "resource_manager.h"
#include "scene.h"
#include "mesh.h"
#include "camera.h"
#include "transform.h"

#include "bounds_utils.h"
#include "visibility_context.h"

#include "frustum_culler.h"
#include "screen_size_culling.h"
#include "distance_culling.h"
#include "occlusion_culler.h"

namespace Engine {

namespace {

bool computeViewContext(
    glm::mat4& view,
    glm::mat4& projection,
    glm::vec3& cameraPosition,
    const Scene& scene
) {
    bool found = false;
    scene.forEach<Camera>([&](EntityId id, const Camera& camera) {
        if (found) return;
        if (!camera.active) return;
        if (!scene.has<Transform>(id)) return;

        const auto& transform = scene.get<Transform>(id);

        projection     = Camera::computeProjection(camera);
        view           = Transform::computeView(transform);
        cameraPosition = transform.position;
        found = true;
    });

    return found;
}

} // anonymous

Visibility buildVisibility(
    Scene& scene,
    const ResourceManager& resources,
    uint32_t viewportWidth,
    uint32_t viewportHeight
) {
    Visibility result;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;

    if (!computeViewContext(view, projection, cameraPosition, scene)) {
        LOG_ERROR("No active camera found for visibility");
        return result;
    }

    const glm::mat4 viewProjection = projection * view;

    VisibilityContext context{
        .frustum        = extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .projection     = projection,

        .viewportWidth  = viewportWidth,
        .viewportHeight = viewportHeight,
        .minPixels      = 3.0f,
        .maxDistance    = 500.0f
    };

    result.entities.reserve(scene.count<Mesh>());
    result.modelMatrices.reserve(scene.count<Mesh>());

    // Dense iteration: visits only entities with Mesh components (no holes)
    scene.forEach<Mesh>([&](EntityId id, Mesh& mesh) {
        if (!mesh.visible) return;
        if (!scene.has<Transform>(id)) return;

        const auto& transform = scene.get<Transform>(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) return;

        const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);

        localToWorldAABB(
            modelMatrix,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            mesh.boundsMin,
            mesh.boundsMax
        );

        if (!FrustumCuller::isVisible(mesh, context)) return;
        if (!DistanceCuller::isVisible(mesh, context)) return;
        if (!ScreenSizeCuller::isVisible(mesh, context)) return;

        result.entities.push_back(id);
        result.modelMatrices.push_back(modelMatrix);
    });

    return result;
}

} // namespace Engine
