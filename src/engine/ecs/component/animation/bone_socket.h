#pragma once

#include <cstdint>
#include <string>

#include "core/reflect.h"
#include "ecs/component/core/transform.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief What a weapon, a hat or a muzzle flash carries to ride a bone of the
 *        rig it hangs off.
 *
 * The socket is the attached entity itself, not a marker something else is
 * parented to. A separate marker would be a second entity per attachment, with
 * a Transform nobody authors, listed in the hierarchy and written to the scene
 * file - the same cost per attachment that an entity per bone would have been
 * per joint, for no information the socket does not already carry.
 *
 * The entity must be a direct child of the entity carrying the Animator. That
 * is not a convenience: BoneSocketSystem writes this entity's *local* Transform
 * and lets HierarchySystem resolve it later in the same frame, which is what
 * keeps the socket on the bone the frame the character moves instead of one
 * behind. `parentWorld * local` only lands on the bone when the parent's world
 * matrix is the rig's, so a socket parented anywhere else is refused and named
 * rather than placed somewhere plausible and wrong.
 *
 * It also means the rig needs no EntityId here - it is whoever the Hierarchy
 * says the parent is - so a socket survives prefabs, undo and scene load with
 * nothing to remap, exactly like the skinned meshes beside it.
 *
 * The bone is named, never indexed. An index is what the pose arrays are
 * addressed by and it is one lookup cheaper, but it is a property of one export
 * of one rig: re-export a character with a joint inserted and every stored index
 * silently addresses its neighbour, which is a weapon that moves to the elbow
 * and no error anywhere. The name is the joint's durable identity - it is what
 * a clip binds by at cook time - so it is what an authored socket stores, for
 * the reason a prefab override stores a uid rather than a row number.
 */
struct BoneSocket {
    std::string bone;    ///< Bone name in the rig above; empty places nothing.
    Transform   offset;  ///< Placement relative to that bone, in bone space.

    // Transient: what `bone` resolved to and what it resolved against, so the
    // rig's linear name lookup happens when the pairing changes rather than
    // every frame. Never serialized - an index describes the rig currently
    // loaded, not the socket that was authored.
    SkeletonHandle resolvedRig;     ///< Rig `boneIndex` was resolved against.
    std::string    resolvedName;    ///< Value of `bone` at that resolve.
    int32_t        boneIndex = -1;  ///< Bone `bone` names, or -1 when the rig has none.
};

} // namespace Vkm::Engine

// The resolved triple is transient runtime state and intentionally absent: only
// the authored pairing of a bone name with an offset is serialized.
VKM_REFLECT_BEGIN(::Vkm::Engine::BoneSocket)
    VKM_F(bone),
    VKM_F(offset)
VKM_REFLECT_END()
