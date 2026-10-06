#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

int32_t SkeletonAsset::indexOf(std::string_view name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int32_t>(i);
    }
    return -1;
}

std::string findSkeletonFault(const SkeletonAsset& skeleton) {
    const size_t count = skeleton.bones.size();
    if (skeleton.inverseBind.size() != count || skeleton.bindPose.size() != count) {
        return std::to_string(count) + " bone(s) against "
            + std::to_string(skeleton.inverseBind.size()) + " inverse-bind and "
            + std::to_string(skeleton.bindPose.size()) + " bind-pose entries";
    }
    // A later or self parent would make the forward compose read an unwritten transform.
    for (size_t i = 0; i < count; ++i) {
        const int32_t parent = skeleton.bones[i].parent;
        if (parent < -1 || parent >= static_cast<int32_t>(i)) {
            return "bone " + std::to_string(i) + " names parent " + std::to_string(parent)
                + ", which is not a bone before it";
        }
    }
    return {};
}

} // namespace Vkm::Engine
