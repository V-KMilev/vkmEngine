#pragma once

#include "ecs/entity.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief How a ragdoll is proportioned, in the rig's own units.
 */
struct RagdollSettings {
    /**
     * @brief Bone radius as a fraction of its length.
     *
     * A limb is a capsule spanning bone to child, and its thickness has to come
     * from somewhere: a rig carries no notion of how solid it is. A fraction of
     * length is the one guess that scales - it makes a forearm thinner than a
     * thigh without anyone measuring either.
     */
    float thickness = 0.22f;

    /**
     * @brief Shortest bone that gets a body of its own, in metres.
     *
     * Fingers and toe tips are bones, and giving each one a body and two joints
     * buys nothing a viewer can see while costing a constraint apiece. Below
     * this a bone follows its parent.
     */
    float minBoneLength = 0.06f;

    float mass = 70.0f;        ///< Total mass, shared out by limb volume
    float stiffness = 1.0f;    ///< Passed to every joint the build makes

    /**
     * @brief Collision layer the bones are put on, as a single bit.
     *
     * The bones sit inside whatever collider the character already has, and the
     * build takes this bit out of the owner's mask - the one relationship it
     * can safely decide, since a rig's bones and the body they hang off are
     * never two things that should push each other.
     *
     * The contacts a shared layer would generate are meaningless - a body
     * cannot be pushed out of itself, and every tick spent resolving that is
     * spent on nothing.
     *
     * Everything else still hits them, so they serve as hit boxes while the
     * ragdoll is inactive and as a body when it is not.
     */
    int boneLayer = 1 << 1;
};

/**
 * @brief Build bodies and joints shaped like @p rig, and attach a Ragdoll.
 *
 * One capsule per bone long enough to matter, spanning it to its first child,
 * and a point joint to the parent's body at the bone's own origin. The result
 * is ordinary physics: nothing in the solver knows a rig is involved, which is
 * what lets a ragdoll stack, sleep and collide like anything else.
 *
 * Idempotent by replacement: an entity that already carries a Ragdoll has its
 * bodies destroyed and rebuilt, so a rebuild after a rig change cannot leave
 * half a skeleton behind.
 *
 * @param scene Scene the bodies are created in.
 * @param rigEntity Entity the Ragdoll is added to. The bones are grouped under
 *        a node of their own, created as its child, so a rig lives inside the
 *        character it belongs to without burying whatever else it owns. It must
 *        not carry a scale: the solver ignores Transform
 *        scale and the hierarchy does not, so a scaled parent puts every limb
 *        somewhere the two disagree about. Art in the wrong units is scaled on
 *        the node that draws it, never on the node that collides.
 *
 *        The bones are *placed* in the frame of the Animator at or below it,
 *        which a model import puts on a child rather than on the entity the
 *        physics is authored on - so the frame and the parent are found
 *        separately and are usually not the same entity.
 * @param rig The skeleton to mirror.
 * @param settings Proportions.
 * @return How many bodies were created; zero when the rig has no usable bones.
 */
uint32_t buildRagdoll(Scene& scene, EntityId rigEntity, const SkeletonAsset& rig,
                      const RagdollSettings& settings = {});

/**
 * @brief Destroy a ragdoll's bodies and drop the component.
 *
 * @param scene Scene holding them.
 * @param rigEntity Entity carrying the Ragdoll.
 */
void clearRagdoll(Scene& scene, EntityId rigEntity);

} // namespace Vkm::Engine
