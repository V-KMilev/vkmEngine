#pragma once

#include <vector>
#include <string>

#include "resource/resource.h"
#include "resource/resource_handle.h"
#include "resource/texture_format.h"

namespace Engine {

/**
 * @brief Texture asset combining Resource tracking with engine-level texture parameters.
 *
 * Uses engine-level TextureParams (backend-agnostic) instead of GL-specific types.
 * The backend is responsible for converting these to API-specific formats during GPU upload.
 */
struct TextureAsset : public Resource {
    TextureParams params;                          ///< Backend-agnostic texture parameters.
    std::vector<uint8_t> pixelData = {};           ///< Pixel data for the texture.
    bool srgb                      = false;        ///< Whether to use sRGB color space.
    std::string filePath           = "";           ///< Optional file path if loaded from disk.
};

using TextureHandle = Handle<TextureAsset>;

} // namespace Engine
