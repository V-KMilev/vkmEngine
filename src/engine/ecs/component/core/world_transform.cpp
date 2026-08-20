#include "ecs/component/core/world_transform.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine {

glm::mat4 resolvedWorldMatrix(const Scene& scene, EntityId entity, const Transform& local) {
    if (scene.has<WorldTransform>(entity)) return scene.get<WorldTransform>(entity).model;
    return Transform::computeModelMatrix(local);
}

glm::vec3 resolvedWorldPosition(const Scene& scene, EntityId entity, const Transform& local) {
    if (scene.has<WorldTransform>(entity)) return glm::vec3(scene.get<WorldTransform>(entity).model[3]);
    return local.position;
}

glm::quat resolvedWorldRotation(const Scene& scene, EntityId entity, const Transform& local) {
    if (scene.has<WorldTransform>(entity)) return Math::worldRotationOf(scene.get<WorldTransform>(entity).model);
    return local.rotation;
}

} // namespace Vkm::Engine
