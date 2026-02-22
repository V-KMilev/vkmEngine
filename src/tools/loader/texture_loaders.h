#pragma once

#include <string>

#include "resource/texture_asset.h"
#include "resource/resource_handle.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Load a texture from a file.
 * 
 * Supports common image formats: PNG, JPG, TGA, BMP, etc.
 * Uses stb_image internally for loading.
 * 
 * @param filePath Path to the image file.
 * @param resourceManager Resource manager to add the texture to.
 * @param srgb Whether to use sRGB color space (true for albedo, false for data textures).
 * @param generateMipmaps Whether to generate mipmaps.
 * @return Handle to the loaded texture, or invalid handle on failure.
 */
TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb = false,
    bool generateMipmaps = true
);

} // namespace Engine
