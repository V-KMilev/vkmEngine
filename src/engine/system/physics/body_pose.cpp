#include "system/physics/body_pose.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

BodyPose worldPoseOf(Scene& scene, EntityId id, const Transform& local) {
    BodyPose pose;
    pose.position = local.position;
    pose.rotation = local.rotation;

    if (!scene.has<Hierarchy>(id)) return pose;
    const EntityId parent = scene.get<Hierarchy>(id).parent;
    if (!parent) return pose;

    // Walked rather than read out of WorldTransform. That component is written
    // by the Transform stage, which runs after this one, so it is a frame stale
    // where it exists and absent entirely on a body parented this tick - a
    // ragdoll's bones, on the tick they are built. Bodies are parented shallowly
    // and only some of them are parented at all, so the walk is cheap and it is
    // right on the first tick as well as every later one.
    const glm::mat4 selfWorld   = HierarchyOperations::computeWorldMatrix(scene, id);
    const glm::mat4 parentWorld = HierarchyOperations::computeWorldMatrix(scene, parent);

    pose.position       = glm::vec3(selfWorld[3]);
    pose.rotation       = Math::worldRotationOf(selfWorld);
    pose.parented       = true;
    pose.parentWorldInv = glm::inverse(parentWorld);
    pose.parentRot      = Math::worldRotationOf(parentWorld);
    return pose;
}

} // namespace Vkm::Engine
