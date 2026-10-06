#pragma once

#include <vector>
#include <string>

#include "resource/resource.h"
#include "resource/resource_handle.h"
#include "resource/texture_format.h"

namespace Vkm::Engine {

/**
 * @brief Texture asset combining Resource tracking with engine-level texture parameters.
 */
struct TextureAsset : public Resource {
    TextureParams params;
    /// Every level params.mipLevels names, level 0 first, tightly packed: texels, or 4x4 blocks for a BC
    /// format.
    std::vector<uint8_t> pixelData = {};
    /// True while an async decode is in flight; AsyncLoaderSystem clears it.
    bool loading                   = false;

    /**
     * @brief Whether the texture stores colour in sRGB.
     *
     * Read off the internal format, which a decode in flight already carries.
     *
     * @return True for SRGB8, SRGBA8 and BC7SRGBA.
     */
    bool isSrgb() const { return isSrgbFormat(params.internalFormat); }

    /**
     * @brief What the texels mean, read off the internal format as isSrgb is.
     *
     * @return The usage the texture was imported with.
     */
    TextureUsage usage() const { return textureUsageOf(params.internalFormat); }
};

using TextureHandle = Handle<TextureAsset>;

} // namespace Vkm::Engine
