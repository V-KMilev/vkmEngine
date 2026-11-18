#pragma once

#include <cstdint>
#include <string>

#include "base/reflect/reflect.h"
#include "ecs/component/scene/transform.h"
#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

/**
 * @brief Carried by a weapon, a hat or a muzzle flash to ride a bone of the rig it hangs off.
 *
 * A direct child of the entity carrying the Animator, naming its bone: the rules and their reasons
 * are docs/reference/animation.md, "Bone sockets".
 */
struct BoneSocket {
    std::string bone;    ///< Bone name in the rig above; empty places nothing.
    Transform   offset;  ///< Placement relative to that bone, in bone space.

    // Transient: what `bone` resolved to and against, so the linear name lookup runs only when the
    // pairing changes. The skeleton as asset and version, so one re-imported under its handle is
    // looked in again.
    uint64_t    resolvedUid     = 0;   ///< The skeleton's Resource::uid `boneIndex` was resolved against.
    uint64_t    resolvedVersion = 0;   ///< Its version then.
    std::string resolvedName;          ///< Value of `bone` at that resolve.
    int32_t     boneIndex       = -1;  ///< Bone `bone` names, or -1 when the rig has none.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::BoneSocket)
    VKM_F(bone, "The bone of the rig above that it follows")
    VKM_F(offset, "Where it sits on that bone")
VKM_REFLECT_END()
