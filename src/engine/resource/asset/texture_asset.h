#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "resource/resource.h"
#include "resource/handle.h"
#include "resource/asset/texture_params.h"

namespace Vkm::Engine {

/**
 * @brief Texture asset combining Resource tracking with engine-level texture parameters.
 */
struct TextureAsset : public Resource {
    TextureParams        params;
    std::vector<uint8_t> pixelData = {};     ///< Every level params.mipLevels names, level 0 first, tightly
                                             ///< packed: texels, or 4x4 blocks for a BC format. Freed once
                                             ///< the backend holds them where the host asks
                                             ///< (RenderSystem::releaseUploadedPixels, as the runtime does),
                                             ///< so a texture with none has not therefore failed; the editor
                                             ///< keeps them to cook from.
    bool                 loading   = false;  ///< True while an async decode is in flight; AsyncLoaderSystem
                                             ///< clears it.

    /**
     * @brief Whether the texture stores color in sRGB.
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

/**
 * @brief Why @p texture cannot be uploaded, or an empty string when it can.
 *
 * What the upload relies on unchecked: one level or the whole chain to 1x1, and pixels
 * that are exactly those levels' bytes. Its enums are taken to name enumerators.
 *
 * @param texture Texture to judge, its pixels held.
 * @return The first fault found, worded to follow "cannot be uploaded - ".
 */
std::string findTextureFault(const TextureAsset& texture);

using TextureHandle = Handle<TextureAsset>;

} // namespace Vkm::Engine
