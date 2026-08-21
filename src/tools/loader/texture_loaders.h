#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/texture_asset.h"
#include "resource/resource_handle.h"
#include "resource/texture_format.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Read the filter override a texture recipe's optional `filter` key names.
 *
 * The key is written only by a texture that has an opinion about its own
 * sampling, which is the rare one - pixel art, a lookup table, a UI sprite - so
 * an absent or unrecognised value reads as None and the texture follows the
 * scene's filtering setting like everything else.
 *
 * @param source JSON source descriptor for a texture.
 * @return TextureFilterOverride::Nearest for "nearest", None otherwise.
 */
TextureFilterOverride textureFilterFromRecipe(const nlohmann::json& source);

/**
 * @brief Load a texture from a file.
 *
 * Decoded with stb_image: PNG, JPG, TGA, BMP and the rest of its formats.
 *
 * @param filePath Path to the image file.
 * @param resourceManager Resource manager to add the texture to.
 * @param srgb Whether to use sRGB color space (true for albedo, false for data textures).
 * @param generateMipmaps Whether to generate mipmaps.
 * @param filterOverride The texture's own say over its sampling; None leaves it
 *        to the scene's filtering setting, which is what ordinary art wants.
 * @return Handle to the loaded texture, or invalid handle on failure.
 */
TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb = false,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None
);

/**
 * @brief Import a texture without blocking, finalised on a later frame.
 *
 * Returns immediately with a valid handle; the asset starts in a `loading`
 * state with no pixel data and is finalised by AsyncLoaderSystem on a later
 * frame (typically 1-3 frames out, depending on decode time). Until finalised
 * the texture renders as undefined contents (the GL backend allocates storage
 * at first sync but doesn't fill it). For critical visuals where a pop-in is
 * unacceptable, use synchronous loadTexture(); for streaming / large-scene
 * workloads, async is the point.
 *
 * Idempotent: requesting the same path twice (within a session) returns the
 * same handle - findByName(path) dedup at the resource layer.
 *
 * @param filePath Path to the image file to import.
 * @param resourceManager Resource manager the stub texture is added to.
 * @param srgb Whether to use sRGB color space (true for albedo, false for data textures).
 * @param generateMipmaps Whether to generate mipmaps once the pixels are decoded.
 * @param filterOverride The texture's own say over its sampling; None leaves it
 *        to the scene's filtering setting, which is what ordinary art wants.
 * @return Handle to the loading texture; valid immediately, filled on a later frame.
 */
TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb = false,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None
);

} // namespace Vkm::Engine
