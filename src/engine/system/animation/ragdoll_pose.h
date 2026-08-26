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
 * The split exists for the thread the composing runs on. The pose pass runs
 * inside a parallel loop whose safety argument is that workers never touch the
 * scene; reading the bodies is scene work, so it happens in the single-threaded
 * gather and travels to the worker as plain values.
 */
struct RagdollBodyPose {
    glm::mat4 world = glm::mat4(1.0f);  ///< The body's world matrix
    bool simulated = false;             ///< False: the bone follows its parent
};

/**
 * @brief Read every bone body's world pose for one ragdoll.
 *
 * Walks the hierarchy itself rather than reading WorldTransform: bones are
 * children of the character, and the pass that resolves world transforms runs
 * after the pose is composed. A bone whose body is dead or missing is marked
 * unsimulated and follows its parent through the bind pose.
 *
 * @param scene Scene holding the bodies.
 * @param ragdoll The mapping from bones to bodies.
 * @return One entry per ragdoll bone, in the same order.
 */
std::vector<RagdollBodyPose> gatherRagdollBodies(const Scene& scene,
                                                 const Ragdoll& ragdoll);

/**
 * @brief Compose a rig's pose from the bodies simulating it.
 *
 * The other half of a ragdoll. The bodies are ordinary physics and the solver
 * knows nothing about rigs, so this is where the two meet: each simulated
 * bone's model transform is its body's world transform brought back into the
 * rig's frame and through the offset recorded when the ragdoll was built.
 *
 * A bone with no body of its own follows its parent through its bind pose,
 * which is what lets fingers and toes be skipped without the hand coming off.
 * Bones are walked parent before child, so a parent's model transform is always
 * final by the time a child needs it - the same invariant composePose relies on.
 *
 * Touches no scene: the bodies arrive through @p bodies, gathered beforehand,
 * which is what lets this run inside the animation system's parallel pass.
 *
 * @param ragdoll The mapping from bones to bodies.
 * @param bodies The bodies' world poses, from gatherRagdollBodies.
 * @param skeleton Rig being posed.
 * @param rigWorld The rig entity's world matrix; the pose is relative to it.
 * @param out Slice to write, sized for the skeleton's bone count.
 */
void composeRagdollPose(
    const Ragdoll& ragdoll,
    const std::vector<RagdollBodyPose>& bodies,
    const SkeletonAsset& skeleton,
    const glm::mat4& rigWorld,
    const PoseWrite& out
);

} // namespace Vkm::Engine
