#pragma once

#include <vector>
#include <string>

#include "resource/resource.h"
#include "resource/resource_handle.h"
#include "resource/texture_format.h"

namespace Vkm::Engine {

/**
 * @brief Texture asset combining Resource tracking with engine-level texture parameters.
 *
 * The backend converts TextureParams to API-specific formats on upload.
 */
struct TextureAsset : public Resource {
    TextureParams params;
    std::vector<uint8_t> pixelData = {};           ///< Decoded pixels, tightly packed.
    bool srgb                      = false;        ///< Selects the SRGB internal formats on upload.
    bool loading                   = false;        ///< True while an async decode is in flight; flipped false when the AsyncLoader finalises the upload.
    std::string filePath           = "";           ///< Optional file path if loaded from disk.
};

using TextureHandle = Handle<TextureAsset>;

} // namespace Vkm::Engine
