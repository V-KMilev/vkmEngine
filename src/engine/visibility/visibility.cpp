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

/**
 * @brief Type alias for a pair of an entity ID and a model matrix.
 */
using VisiblePair = std::pair<EntityId, glm::mat4>;

bool computeViewContext(
    glm::mat4& view,
    glm::mat4& projection,
    glm::vec3& cameraPosition,
    const Scene& scene
) {
    const auto& cameraStorage     = scene.storage<Camera>();
    const auto& transformStorage  = scene.storage<Transform>();

    for (EntityId id = 0; id < cameraStorage.size(); ++id) {
        if (!cameraStorage.has(id)) continue;

        const auto& camera = cameraStorage.get(id);
        if (!camera.active) continue;

        if (!transformStorage.has(id)) continue;

        const auto& transform = transformStorage.get(id);

        projection     = Camera::computeProjection(camera);
        view           = Transform::computeView(transform);
        cameraPosition = transform.position;
        return true;
    }

    return false;
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

    auto& meshStorage            = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

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

    const size_t total = meshStorage.size();

    for (EntityId id = 0; id < total; ++id) {
        if (!meshStorage.has(id)) continue;
        if (!transformStorage.has(id)) continue;

        Mesh& mesh = meshStorage.get(id);
        if (!mesh.visible) continue;

        const auto& transform = transformStorage.get(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        if (!hasValidBounds(meshAsset.boundsMin, meshAsset.boundsMax)) continue;

        const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);

        localToWorldAABB(
            modelMatrix,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            mesh.boundsMin,
            mesh.boundsMax
        );

        if (!FrustumCuller::isVisible(mesh, context)) continue;
        if (!DistanceCuller::isVisible(mesh, context)) continue;
        if (!ScreenSizeCuller::isVisible(mesh, context)) continue;

        result.entities.push_back(id);
        result.modelMatrices.push_back(modelMatrix);
    }

    return result;
}

} // namespace Engine
