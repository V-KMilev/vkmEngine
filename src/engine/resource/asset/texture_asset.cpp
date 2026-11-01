#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

std::string findTextureFault(const TextureAsset& texture) {
    const TextureParams& params = texture.params;
    const std::string size = std::to_string(params.width) + "x" + std::to_string(params.height);
    // A mipmap filter over a partial chain leaves the texture incomplete, which samples black.
    const uint32_t chain = mipChainLength(params.width, params.height);
    if (chain == 0 || (params.mipLevels != 1 && params.mipLevels != chain)) {
        return std::to_string(params.mipLevels) + " level(s) for " + size + ", which has "
            + std::to_string(chain);
    }
    // The upload reads exactly each level's bytes. Bounded level by level by division, so sides read
    // off disk cannot wrap the sum.
    const uint64_t unitBytes = textureUnitBytes(params);
    uint64_t left = texture.pixelData.size();
    bool fits = true;
    for (uint32_t level = 0; fits && level < params.mipLevels; ++level) {
        const uint64_t units = textureLevelUnits(params, level);
        fits = units <= left / unitBytes;
        if (fits) left -= units * unitBytes;
    }
    if (!fits || left != 0) {
        return std::to_string(texture.pixelData.size()) + " pixel byte(s) are not "
            + std::to_string(params.mipLevels) + " level(s) of " + size;
    }
    return {};
}

} // namespace Vkm::Engine
