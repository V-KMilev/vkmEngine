#pragma once

#include <vector>
#include <string>

#include "resource.h"
#include "resource_handle.h"

#include "texture/gl_texture.h"

namespace Engine {

/**
 * @brief Texture asset that combines Resource tracking with Core::Texture2DParams.
 * 
 * TextureAsset inherits from both Resource (for version tracking) and Core::Texture2DParams
 * (for texture parameters), allowing direct use of Core texture types without duplication.
 * 
 * The data vector stores the pixel data. The data pointer in Texture2DParams should be
 * set to point to this vector's data before using TextureAsset to create a Core::Texture2D.
 */
struct TextureAsset : public Resource, public Core::Texture2DParams {
    std::vector<uint8_t> pixelData = {};       ///< Pixel data for the texture.
    bool srgb                      = false;    ///< Whether to use sRGB color space.
    std::string filePath           = "";       ///< Optional file path if loaded from disk.

    /**
     * @brief Get Texture2DParams with data pointer properly set.
     * 
     * Returns a reference to the base Texture2DParams with the data pointer
     * synchronized to point to the pixelData vector.
     * 
     * @return Reference to Texture2DParams with synchronized data pointer.
     */
    Core::Texture2DParams& getParams() {
        data = pixelData.empty() ? nullptr : pixelData.data();
        return *this;
    }

    /**
     * @brief Get const Texture2DParams with data pointer properly set.
     * 
     * @return Const reference to Texture2DParams with synchronized data pointer.
     */
    const Core::Texture2DParams& getParams() const {
        // Const cast is safe here as we're only reading the data
        const_cast<TextureAsset*>(this)->data = pixelData.empty() ? nullptr : pixelData.data();
        return *this;
    }
};

using TextureHandle = Handle<TextureAsset>;

} // namespace Engine
