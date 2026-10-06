#include "ecs/component/core/world_transform.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

glm::mat4 resolvedWorldMatrix(const Scene& scene, EntityId entity, const Transform& local) {
    const WorldTransform* world = scene.tryGet<WorldTransform>(entity);
    return world ? world->model : Transform::computeModelMatrix(local);
}

glm::vec3 resolvedWorldPosition(const Scene& scene, EntityId entity, const Transform& local) {
    const WorldTransform* world = scene.tryGet<WorldTransform>(entity);
    return world ? glm::vec3(world->model[3]) : local.position;
}

glm::quat resolvedWorldRotation(const Scene& scene, EntityId entity, const Transform& local) {
    const WorldTransform* world = scene.tryGet<WorldTransform>(entity);
    return world ? Math::worldRotationOf(world->model) : local.rotation;
}

glm::vec3 resolvedWorldScale(const Scene& scene, EntityId entity, const Transform& local) {
    const WorldTransform* world = scene.tryGet<WorldTransform>(entity);
    if (!world) return local.scale;
    return glm::vec3(
        glm::length(glm::vec3(world->model[0])),
        glm::length(glm::vec3(world->model[1])),
        glm::length(glm::vec3(world->model[2]))
    );
}

} // namespace Vkm::Engine
