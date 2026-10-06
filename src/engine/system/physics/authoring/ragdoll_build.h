#pragma once

#include "ecs/entity.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief How a ragdoll is proportioned and weighted.
 */
struct RagdollSettings {
    /**
     * @brief Bone radius as a fraction of its length; a rig carries no thickness of its own.
     */
    float thickness = 0.22f;

    /**
     * @brief Shortest bone that gets a body of its own, in metres; a shorter one follows its parent.
     */
    float minBoneLength = 0.06f;

    float mass      = 70.0f;  ///< Total mass, shared out by limb volume
    float stiffness = 1.0f;   ///< Passed to every joint the build makes

    /**
     * @brief How much of the body's shape it keeps once limp, 0 to 1.
     *
     * Each joint's Joint::holdTorque is this fraction of what holds every limb beyond it out level. 0 is
     * a rag that folds where it stands; 1 holds its pose against its own weight, as a mannequin would.
     */
    float muscle = 0.4f;

    /**
     * @brief Collision layer the bones are put on, as a single bit.
     *
     * The build takes it out of the owner's mask, so the bones and the collider they sit inside never
     * push each other, and out of the bones' own mask, since overlapping limbs would spend the first tick
     * resolving their authored interpenetration. Everything else hits them, as hit boxes while inactive.
     */
    int boneLayer = 1 << 1;
};

/**
 * @brief Build bodies and joints shaped like @p rig, and attach a Ragdoll.
 *
 * One capsule per bone long enough to matter, spanning it to its first child, and a point joint to the
 * parent's body at the bone's origin. An existing Ragdoll's bodies are destroyed and rebuilt.
 *
 * @param scene Scene the bodies are created in.
 * @param rigEntity Gets the Ragdoll; the bones are grouped under a new child. Must not be scaled: a
 *        ColliderPart is unscaled and the hierarchy is not. The bones are *placed* in the frame of the
 *        Animator at or below it, which importModelIntoScene can put on a child.
 * @param rig The skeleton to mirror.
 * @param settings Proportions.
 * @return Bodies created; zero when the rig has no usable bones.
 */
uint32_t buildRagdoll(
    Scene& scene,
    EntityId rigEntity,
    const SkeletonAsset& rig,
    const RagdollSettings& settings = {}
);

/**
 * @brief Destroy a ragdoll's bodies and drop the component.
 *
 * @param scene Scene holding them.
 * @param rigEntity Entity carrying the Ragdoll.
 */
void clearRagdoll(Scene& scene, EntityId rigEntity);

} // namespace Vkm::Engine
