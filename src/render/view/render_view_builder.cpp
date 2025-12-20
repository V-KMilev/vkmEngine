#include "render_view_builder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "scene.h"
#include "transform.h"
#include "camera.h"
#include "mesh.h"

namespace Engine {

RenderView RenderViewBuilder::build(const Scene& scene) {
    RenderView renderView;

    bool foundCamera = false;

    // Find the active camera using storage iteration
    const auto& cameraStorage = scene.storage<Camera>();
    const auto& transformStorage = scene.storage<Transform>();

    for (EntityId id = 0; id < cameraStorage.size(); ++id) {
        if (!cameraStorage.has(id)) {
            continue;
        }

        const auto& camera = cameraStorage.get(id);
        if (!camera.active) {
            continue;
        }

        // Get transform for this camera entity
        if (!transformStorage.has(id)) {
            continue;
        }

        const auto& transform = transformStorage.get(id);

        // Compute camera position and orientation vectors
        renderView.camera.position = transform.position;
        glm::vec3 forward          = Transform::computeForward(transform.rotation);
        glm::vec3 up               = Transform::computeUp(transform.rotation);

        renderView.camera.view = glm::lookAt(
            renderView.camera.position,
            renderView.camera.position + forward,
            up
        );
        renderView.camera.projection = Camera::computeProjection(camera);
        renderView.camera.viewProjection = renderView.camera.projection * renderView.camera.view;

        foundCamera = true;
        break;
    }

    // TODO: Handle this case
    if (!foundCamera) {
        LOG_ERROR("No active camera found");
    }

    // Gather drawables using storage iteration
    const auto& meshStorage = scene.storage<Mesh>();

    // Reserve space (estimate based on mesh storage size)
    renderView.drawables.reserve(meshStorage.size());

    for (EntityId id = 0; id < meshStorage.size(); ++id) {
        if (!meshStorage.has(id)) {
            continue;
        }

        const auto& mesh = meshStorage.get(id);
        if (!mesh.visible) {
            continue;
        }

        // Get transform for this mesh entity
        if (!transformStorage.has(id)) {
            continue;
        }

        const auto& transform = transformStorage.get(id);

        // Compute model matrix
        DrawableData drawable;
        drawable.mesh     = mesh.mesh;
        drawable.material = mesh.material;
        drawable.model    = Transform::computeModelMatrix(transform);

        renderView.drawables.emplace_back(drawable);
    }

    return renderView;
}

} // namespace Engine