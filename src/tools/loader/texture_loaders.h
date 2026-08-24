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
 * @brief Read the wrap mode a texture recipe's optional `wrap` key names.
 *
 * The key is written only by a texture that tiles - a model's maps, which
 * reference UVs outside [0,1] - so an absent or unrecognised value reads as
 * ClampToEdge, which is what a recipe without the key has always meant.
 *
 * @param source JSON source descriptor for a texture.
 * @return The mode the key names, or TextureWrapMode::ClampToEdge.
 */
TextureWrapMode textureWrapFromRecipe(const nlohmann::json& source);

/**
 * @brief Build the `kind: file` recipe descriptor a texture is re-created from.
 *
 * `filter` and `wrap` are written only when the texture states something other
 * than the default, so an ordinary texture's recipe says nothing about either -
 * which is the truth about it, and one fewer spelling of the default to keep in
 * step. This is the only writer of the descriptor: a producer that decodes its
 * own pixels stamps what this returns rather than spelling the keys again, or
 * one of them ends up written by one producer and not the other.
 *
 * @param ref Project-relative reference the texture is named and reloaded by.
 * @param srgb Whether the pixels are sRGB-encoded.
 * @param generateMipmaps Whether a mip chain is built for it.
 * @param filterOverride The texture's own say over its sampling.
 * @param wrap How sampling behaves outside [0,1], on both axes.
 * @return The `kind: file` source descriptor.
 */
nlohmann::json fileTextureRecipe(
    const std::string& ref,
    bool srgb,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
);

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
 * @param wrap How sampling behaves outside [0,1]; set on both axes, and carried
 *        in the recipe so a texture rebuilt from it tiles the same way.
 * @return Handle to the loaded texture, or invalid handle on failure.
 */
TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb = false,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None,
    TextureWrapMode wrap = TextureWrapMode::ClampToEdge
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
 * @param wrap How sampling behaves outside [0,1]; set on both axes, and carried
 *        in the recipe so a texture rebuilt from it tiles the same way.
 * @return Handle to the loading texture; valid immediately, filled on a later frame.
 */
TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    bool srgb = false,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None,
    TextureWrapMode wrap = TextureWrapMode::ClampToEdge
);

} // namespace Vkm::Engine
