#pragma once

#include <cstddef>
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
 * fileTextureRecipe writes the key only for a texture that has an opinion about
 * its own sampling, so an absent or unrecognised value reads as None and the
 * texture follows the scene's filtering setting.
 *
 * @param source JSON source descriptor for a texture.
 * @return TextureFilterOverride::Nearest for "nearest", None otherwise.
 */
TextureFilterOverride textureFilterFromRecipe(const nlohmann::json& source);

/**
 * @brief Read the wrap mode a texture recipe's optional `wrap` key names.
 *
 * fileTextureRecipe writes the key only for a texture that does not clamp, so
 * an absent or unrecognised value reads as ClampToEdge.
 *
 * @param source JSON source descriptor for a texture.
 * @return The mode the key names, or TextureWrapMode::ClampToEdge.
 */
TextureWrapMode textureWrapFromRecipe(const nlohmann::json& source);

/**
 * @brief Read whether a texture recipe builds a mip chain.
 *
 * fileTextureRecipe always writes the key; a recipe without it reads as true,
 * which is what a texture is unless it says otherwise.
 *
 * @param source JSON source descriptor for a texture.
 * @return The recipe's `generateMipmaps`, or true.
 */
bool textureMipmapsFromRecipe(const nlohmann::json& source);

/**
 * @brief Build the `kind: file` recipe descriptor a texture is re-created from.
 *
 * `filter` and `wrap` are written only when the texture states something other
 * than the default, so an ordinary texture's recipe says nothing about either.
 * This is the only writer of the descriptor: a producer that decodes its own
 * pixels stamps what this returns rather than spelling the keys again.
 *
 * @param ref Project-relative reference the texture is named and reloaded by.
 * @param usage What the texels mean, which decides how they are decoded.
 * @param generateMipmaps Whether a mip chain is built for it.
 * @param filterOverride The texture's own say over its sampling.
 * @param wrap How sampling behaves outside [0,1], on both axes.
 * @return The `kind: file` source descriptor.
 */
nlohmann::json fileTextureRecipe(
    const std::string& ref,
    TextureUsage usage,
    bool generateMipmaps,
    TextureFilterOverride filterOverride,
    TextureWrapMode wrap
);

/**
 * @brief Decode an encoded image held in memory - a PNG or JPG embedded in a
 *        model - by the rule every texture import follows (decodeChannels).
 *
 * Fills the size, the formats and the pixels; the name, the recipe, the wrap
 * and the mip policy are the caller's.
 *
 * @param bytes The encoded file.
 * @param size  Its length in bytes.
 * @param usage What the texels mean, which decides how they are stored.
 * @param out   Receives the decoded texture; untouched on failure.
 * @return False, with stbi_failure_reason() saying why, when it does not decode.
 */
bool decodeTextureFromMemory(const unsigned char* bytes, size_t size, TextureUsage usage, TextureAsset& out);

/**
 * @brief Load a texture from a file.
 *
 * Decoded with stb_image: PNG, JPG, TGA, BMP and the rest of its formats.
 *
 * @param filePath Path to the image file.
 * @param resourceManager Resource manager to add the texture to.
 * @param usage What the texels mean - colour, data or a normal - which decides
 *        how the file's channels are decoded and stored (decodeChannels).
 * @param generateMipmaps Whether to generate mipmaps.
 * @param filterOverride The texture's own say over its sampling; None leaves it
 *        to the scene's filtering setting.
 * @param wrap How sampling behaves outside [0,1]; set on both axes, and carried
 *        in the recipe so a texture rebuilt from it tiles the same way.
 * @return Handle to the loaded texture, or invalid handle on failure.
 */
TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    TextureUsage usage = TextureUsage::Data,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None,
    TextureWrapMode wrap = TextureWrapMode::ClampToEdge
);

/**
 * @brief Import a texture without blocking, finalised on a later frame.
 *
 * Returns immediately with a valid handle; the asset starts in a `loading`
 * state with no pixel data and is finalised by AsyncLoaderSystem on a later
 * frame. Until finalised the texture has no pixels, so GLMaterial::bindTextures
 * substitutes GLView's missing-texture checker rather than sampling unfilled
 * storage. For critical visuals where a pop-in is unacceptable, use synchronous
 * loadTexture().
 *
 * Idempotent: requesting the same path twice (within a session) returns the
 * same handle - findByName(path) dedup at the resource layer.
 *
 * @param filePath Path to the image file to import.
 * @param resourceManager Resource manager the stub texture is added to.
 * @param usage What the texels mean - colour, data or a normal - which decides
 *        how the file's channels are decoded and stored (decodeChannels).
 * @param generateMipmaps Whether to generate mipmaps once the pixels are decoded.
 * @param filterOverride The texture's own say over its sampling; None leaves it
 *        to the scene's filtering setting.
 * @param wrap How sampling behaves outside [0,1]; set on both axes, and carried
 *        in the recipe so a texture rebuilt from it tiles the same way.
 * @return Handle to the loading texture; valid immediately, filled on a later frame.
 */
TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    TextureUsage usage = TextureUsage::Data,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None,
    TextureWrapMode wrap = TextureWrapMode::ClampToEdge
);

} // namespace Vkm::Engine
