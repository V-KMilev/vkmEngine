#include "render_view_builder.h"

#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "scene.h"

#include "transform.h"
#include "camera.h"
#include "mesh.h"

namespace Engine {

static glm::mat4 makePerspective(float fovY, float aspect, float zNear, float zFar) {
    return glm::perspective(fovY, aspect, zNear, zFar);
}

static glm::mat4 makeOrthographic(float halfHeight, float aspect, float zNear, float zFar) {
    const float halfWidth = halfHeight * aspect;
    return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, zNear, zFar);
}

RenderView RenderViewBuilder::build(const Scene& scene) {
    RenderView renderView;


    bool foundCamera = false;

    // Find the active camera
    for (const auto& entity : scene.getEntities()) {
        auto camera = Scene::findComponentAs<Camera>(entity, ComponentType::Camera);
        if (!camera || !camera->isActive()) {
            continue;
        }

        auto transform = Scene::findComponentAs<Transform>(entity, ComponentType::Transform);
        if (!transform) {
            continue;
        }

        renderView.camera.position = transform->getPosition();
        const glm::vec3& forward   = transform->getForward();
        const glm::vec3& up        = transform->getUp();

        renderView.camera.view = glm::lookAt(renderView.camera.position, renderView.camera.position + forward, up);

        if (camera->getProjectionType() == ProjectionType::Perspective) {
            renderView.camera.projection = makePerspective(
                camera->getFovY(),
                camera->getAspect(),
                camera->getNearPlane(),
                camera->getFarPlane()
            );
        } else {
            // Treat OrthoHeight as HALF-height (matches common usage)
            renderView.camera.projection = makeOrthographic(
                camera->getOrthoHeight(),
                camera->getAspect(),
                camera->getNearPlane(),
                camera->getFarPlane()
            );
        }

        renderView.camera.viewProjection = renderView.camera.projection * renderView.camera.view;

        foundCamera = true;
        break;
    }

    // TODO: Handle this case
    if (!foundCamera) {
        LOG_ERROR("No active camera found");
    }

    // Gather renderable instances
    renderView.instances.reserve(scene.getEntities().size());

    for (const auto& entity : scene.getEntities()) {
        auto mesh = Scene::findComponentAs<Mesh>(entity, ComponentType::Mesh);
        if (!mesh) {
            continue;
        }

        auto transform = Scene::findComponentAs<Transform>(entity, ComponentType::Transform);
        if (!transform) {
            continue;
        }

        InstanceData instance;
        instance.model       = transform->getModelMatrix();
        instance.mesh        = mesh->getMesh();
        instance.material    = mesh->getMaterial();
        instance.visible     = mesh->isVisible();
        instance.castsShadow = mesh->castsShadow();

        renderView.instances.emplace_back(instance);
    }

    return renderView;
}

} // namespace Engine