#include "ecs/component/render/camera.h"

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"

namespace Vkm::Engine {

EntityId findActiveCamera(const Scene& scene, EntityId cached) {
    const Camera* held = scene.tryGet<Camera>(cached);
    if (held && held->active && scene.has<Transform>(cached)) {
        return cached;
    }

    return findLowestSlot<Camera, Transform>(
        scene,
        [](const Camera& camera, const Transform&) { return camera.active; }
    );
}

CameraView viewThrough(
    const Camera& lens,
    const glm::vec3& position,
    const glm::quat& rotation,
    float aspect
) {
    Transform pose;
    pose.position = position;
    pose.rotation = rotation;
    return {Transform::computeView(pose), Camera::computeProjection(lens, aspect), position};
}

std::optional<CameraView> viewThrough(const Scene& scene, EntityId entity, float aspect) {
    const Camera*    lens = scene.tryGet<Camera>(entity);
    const Transform* pose = scene.tryGet<Transform>(entity);
    if (!lens || !pose) return std::nullopt;
    const glm::vec3 position = resolvedWorldPosition(scene, entity, *pose);
    const glm::quat rotation = resolvedWorldRotation(scene, entity, *pose);
    return viewThrough(*lens, position, rotation, aspect);
}

} // namespace Vkm::Engine
