#include "system/physics/body_pose.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/hierarchy_operations.h"

namespace Vkm::Engine {

BodyPose worldPoseOf(Scene& scene, EntityId id, const Transform& local) {
    BodyPose pose;
    pose.position = local.position;
    pose.rotation = local.rotation;

    const Hierarchy* link = scene.tryGet<Hierarchy>(id);
    if (!link || !link->parent) return pose;
    const EntityId parent = link->parent;

    // One walk: the parent's chain is a prefix of this entity's.
    const glm::mat4 parentWorld = HierarchyOperations::computeWorldMatrix(scene, parent);
    const glm::mat4 selfWorld   = parentWorld * Transform::computeModelMatrix(local);

    pose.position    = glm::vec3(selfWorld[3]);
    pose.rotation    = Math::worldRotationOf(selfWorld);
    pose.parented    = true;
    pose.parentWorld = parentWorld;
    pose.parentRot   = Math::worldRotationOf(parentWorld);
    return pose;
}

} // namespace Vkm::Engine
