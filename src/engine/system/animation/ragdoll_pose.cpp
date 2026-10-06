#include "system/animation/ragdoll_pose.h"

#include <vector>

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/hierarchy_operations.h"

namespace Vkm::Engine {

namespace {

// thread_local: composeRagdollPose runs on several workers at once, and this avoids allocating per rig.
thread_local std::vector<const RagdollBone*>     t_driver;
thread_local std::vector<const RagdollBodyPose*> t_pose;

} // namespace

void gatherRagdollBodies(const Scene& scene, const Ragdoll& ragdoll, std::vector<RagdollBodyPose>& out) {
    for (const RagdollBone& bone : ragdoll.bones) {
        RagdollBodyPose& pose = out.emplace_back();
        // has() is total: it answers no for a destroyed body too.
        if (!scene.has<Transform>(bone.body)) continue;
        pose.world     = HierarchyOperations::computeWorldMatrix(scene, bone.body);
        pose.simulated = true;
    }
}

void composeRagdollPose(
    const Ragdoll& ragdoll,
    const RagdollBodyPose* bodies,
    const SkeletonAsset& skeleton,
    const glm::mat4& rigWorld,
    const PoseWrite& out
) {
    const size_t count = skeleton.bones.size();

    t_driver.assign(count, nullptr);
    t_pose.assign(count, nullptr);
    for (size_t b = 0; b < ragdoll.bones.size(); ++b) {
        const RagdollBone& entry = ragdoll.bones[b];
        // Authored, and nothing holds it to this rig.
        if (entry.bone < 0 || static_cast<size_t>(entry.bone) >= count) continue;
        t_driver[static_cast<size_t>(entry.bone)] = &entry;
        t_pose[static_cast<size_t>(entry.bone)]   = &bodies[b];
    }

    const glm::mat4 toRig = glm::inverse(rigWorld);

    for (size_t i = 0; i < count; ++i) {
        const RagdollBone* entry = t_driver[i];

        if (entry && t_pose[i]->simulated) {
            out.global[i] = toRig * t_pose[i]->world * entry->bodyFromBone;
        } else {
            const glm::mat4 local = Transform::computeModelMatrix(skeleton.bindPose[i]);
            const int32_t parent = skeleton.bones[i].parent;
            out.global[i] = parent >= 0 ? out.global[parent] * local : local;
        }

        out.palette[i] = out.global[i] * skeleton.inverseBind[i];
    }

    finishSlice(out);
}

} // namespace Vkm::Engine
