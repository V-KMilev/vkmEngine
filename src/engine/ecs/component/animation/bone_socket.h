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
 * parented to: a marker would be a second entity per attachment, with a
 * Transform nobody authors, for no information this does not already carry.
 *
 * The entity must be a direct child of the one carrying the Animator.
 * BoneSocketSystem writes this entity's *local* Transform and lets
 * HierarchySystem resolve it later in the same frame, so `parentWorld * local`
 * only lands on the bone while the parent's world matrix is the rig's - a socket
 * parented anywhere else is refused and named rather than placed somewhere
 * plausible and wrong. It also means the rig needs no EntityId here, so a socket
 * survives prefabs, undo and scene load with nothing to remap.
 *
 * The bone is named, never indexed. An index is a property of one export: insert
 * a joint and every stored index addresses its neighbour, which is a weapon on
 * the elbow and no error anywhere. The name is what a clip binds by at cook time.
 */
struct BoneSocket {
    std::string bone;    ///< Bone name in the rig above; empty places nothing.
    Transform   offset;  ///< Placement relative to that bone, in bone space.

    // Transient: what `bone` resolved to and what it resolved against, so the
    // rig's linear name lookup happens when the pairing changes rather than
    // every frame.
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
