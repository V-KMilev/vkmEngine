#include "visibility/visibility_system.h"

#include "logger.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"

#include "visibility/bounds_utils.h"
#include "visibility/visibility_context.h"

#include "visibility/culling/frustum_culler.h"
#include "visibility/culling/screen_size_culling.h"
#include "visibility/culling/distance_culling.h"
#include "visibility/culling/occlusion_culler.h"

namespace Engine {

namespace {

bool computeViewContext(
    glm::mat4& view,
    glm::mat4& projection,
    glm::vec3& cameraPosition,
    const Scene& scene
) {
    bool found = false;
    scene.forEach<Camera, Transform>([&](EntityId id, const Camera& camera, const Transform& transform) {
        if (found) return;
        if (!camera.active) return;

        projection     = Camera::computeProjection(camera);
        view           = Transform::computeView(transform);
        cameraPosition = transform.position;
        found = true;
    });

    return found;
}

} // anonymous

void VisibilitySystem::update(FrameContext& ctx) {
    Visibility result;

    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;

    if (!computeViewContext(view, projection, cameraPosition, ctx.scene)) {
        LOG_ERROR("No active camera found for visibility");
        ctx.visibility = std::move(result);
        return;
    }

    const glm::mat4 viewProjection = projection * view;

    VisibilityContext context{
        .frustum        = extractFrustum(viewProjection),
        .cameraPosition = cameraPosition,
        .view           = view,
        .projection     = projection,

        .viewportWidth  = ctx.viewportWidth,
        .viewportHeight = ctx.viewportHeight,
        .minPixels      = 3.0f,
        .maxDistance    = 500.0f
    };

    result.entities.reserve(ctx.scene.count<Mesh>());
    result.modelMatrices.reserve(ctx.scene.count<Mesh>());

    ctx.scene.forEach<Mesh, Transform>([&](EntityId id, Mesh& mesh, const Transform& transform) {
        if (!mesh.visible) return;
        const auto& meshAsset = ctx.resources.get(mesh.mesh);

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

    ctx.visibility = std::move(result);
}

} // namespace Engine
