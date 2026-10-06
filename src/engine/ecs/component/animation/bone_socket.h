#pragma once

#include <cstdint>
#include <string>

#include "core/reflect.h"
#include "ecs/component/core/transform.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief Carried by a weapon, a hat or a muzzle flash to ride a bone of the rig it hangs off.
 *
 * The entity must be a direct child of the one carrying the Animator: BoneSocketSystem writes its
 * *local* Transform for HierarchySystem to resolve, which lands on the bone only while the parent's
 * world matrix is the rig's. A socket parented elsewhere is refused and named. No EntityId is stored,
 * so nothing needs remapping.
 *
 * The bone is named, never indexed: inserting a joint shifts every index, and the name is what a clip
 * binds by at cook time.
 */
struct BoneSocket {
    std::string bone;    ///< Bone name in the rig above; empty places nothing.
    Transform   offset;  ///< Placement relative to that bone, in bone space.

    // Transient: what `bone` resolved to and against, so the linear name lookup runs only when the
    // pairing changes.
    SkeletonHandle resolvedRig;     ///< Rig `boneIndex` was resolved against.
    std::string    resolvedName;    ///< Value of `bone` at that resolve.
    int32_t        boneIndex = -1;  ///< Bone `bone` names, or -1 when the rig has none.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::BoneSocket)
    VKM_F(bone)
    VKM_F(offset)
VKM_REFLECT_END()
