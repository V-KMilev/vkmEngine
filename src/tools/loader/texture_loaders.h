#pragma once

#include <string>

#include "resource/asset/texture_asset.h"
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

/**
 * @brief Non-blocking texture import. Returns immediately with a valid
 *        handle; the asset starts in a `loading` state with no pixel
 *        data and is finalised by AsyncLoaderSystem on a later frame
 *        (typically 1-3 frames out, depending on decode time).
 *
 * Until finalised the texture renders as undefined contents (the GL
 * backend allocates storage at first sync but doesn't fill it). For
 * critical visuals where a pop-in is unacceptable, use synchronous
 * loadTexture(); for streaming / large-scene workloads, async is the
 * point.
 *
 * Idempotent: requesting the same path twice (within a session)
 * returns the same handle - AssetDatabase + findById dedup at the
 * resource layer.
 */
TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb = false,
    bool generateMipmaps = true
);

} // namespace Engine
