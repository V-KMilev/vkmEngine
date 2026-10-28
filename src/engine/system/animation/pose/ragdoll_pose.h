#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "resource/asset/skeleton_asset.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

class Scene;
struct Ragdoll;

/**
 * @brief One bone body's world pose, read out of the scene ahead of composing.
 *
 * Gathered on one thread by gatherRagdollBodies, because composeRagdollPose runs on workers that must
 * not touch the scene.
 */
struct RagdollBodyPose {
    glm::mat4 world = glm::mat4(1.0f);  ///< The body's world matrix
    bool simulated = false;             ///< False: the bone follows its parent
};

/**
 * @brief Read every bone body's world pose for one ragdoll.
 *
 * Walks the hierarchy itself: HierarchySystem resolves WorldTransform after the pose is composed. A
 * bone whose body is dead or missing is marked unsimulated.
 *
 * @param scene Scene holding the bodies.
 * @param ragdoll Mapping from bones to bodies.
 * @param out Appended one entry per ragdoll bone, in order.
 */
void gatherRagdollBodies(const Scene& scene, const Ragdoll& ragdoll, std::vector<RagdollBodyPose>& out);

/**
 * @brief Compose a rig's pose from the bodies simulating it.
 *
 * A simulated bone's model transform is its body's world transform brought into the rig's frame and
 * through the offset recorded at build. A bone with no body follows its parent through its bind pose,
 * so fingers and toes can be skipped. Bones are walked parent before child.
 *
 * @param ragdoll Mapping from bones to bodies.
 * @param bodies One world pose per ragdoll bone, in order, as gatherRagdollBodies appended them.
 * @param skeleton Rig being posed; must pass findSkeletonFault, which is not re-checked here.
 * @param rigWorld The rig entity's world matrix; the pose is relative to it.
 * @param out Slice to write, sized for the skeleton's bone count.
 */
void composeRagdollPose(
    const Ragdoll& ragdoll,
    const RagdollBodyPose* bodies,
    const SkeletonAsset& skeleton,
    const glm::mat4& rigWorld,
    const PoseWrite& out
);

} // namespace Vkm::Engine
