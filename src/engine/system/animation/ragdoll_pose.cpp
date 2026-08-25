#include "system/animation/ragdoll_pose.h"

#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/ragdoll.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

std::vector<RagdollBodyPose> gatherRagdollBodies(const Scene& scene,
                                                 const Ragdoll& ragdoll) {
    std::vector<RagdollBodyPose> bodies(ragdoll.bones.size());
    for (size_t i = 0; i < ragdoll.bones.size(); ++i) {
        const EntityId body = ragdoll.bones[i].body;
        if (!body || !scene.isAlive(body) || !scene.has<Transform>(body)) {
            continue;
        }
        bodies[i].world = HierarchyOperations::computeWorldMatrix(scene, body);
        bodies[i].simulated = true;
    }
    return bodies;
}

void composeRagdollPose(
    const Ragdoll& ragdoll,
    const std::vector<RagdollBodyPose>& bodies,
    const SkeletonAsset& skeleton,
    const glm::mat4& rigWorld,
    const PoseWrite& out
) {
    const size_t count = skeleton.bones.size();
    if (count == 0 || !out.slice) return;

    // Which body poses which bone, so the walk below is a lookup rather than a
    // search per bone.
    std::vector<const RagdollBone*> driver(count, nullptr);
    std::vector<const RagdollBodyPose*> pose(count, nullptr);
    for (size_t b = 0; b < ragdoll.bones.size(); ++b) {
        const RagdollBone& entry = ragdoll.bones[b];
        if (entry.bone >= 0 && static_cast<size_t>(entry.bone) < count) {
            driver[static_cast<size_t>(entry.bone)] = &entry;
            if (b < bodies.size()) pose[static_cast<size_t>(entry.bone)] = &bodies[b];
        }
    }

    const glm::mat4 toRig = glm::inverse(rigWorld);

    for (size_t i = 0; i < count; ++i) {
        const RagdollBone* entry = driver[i];
        const bool simulated = entry && pose[i] && pose[i]->simulated;

        if (simulated) {
            out.global[i] = toRig * pose[i]->world * entry->boneFromBody;
        } else {
            // Not simulated: carried by whatever is above it, in the shape it
            // was bound in. A tip bone has nothing to simulate and a skipped
            // one was too short to be worth it; both still have to be posed, or
            // the hand comes off at the wrist.
            const glm::mat4 local = i < skeleton.bindPose.size()
                ? Transform::computeModelMatrix(skeleton.bindPose[i])
                : glm::mat4(1.0f);
            const int32_t parent = skeleton.bones[i].parent;
            out.global[i] = parent >= 0 ? out.global[parent] * local : local;
        }

        out.palette[i] = i < skeleton.inverseBind.size()
            ? out.global[i] * skeleton.inverseBind[i]
            : out.global[i];
    }
}

} // namespace Vkm::Engine
