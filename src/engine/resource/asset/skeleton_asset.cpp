#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

int32_t SkeletonAsset::indexOf(std::string_view name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int32_t>(i);
    }
    return -1;
}

} // namespace Vkm::Engine
