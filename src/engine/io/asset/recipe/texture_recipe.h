#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/texture_params.h"

namespace Vkm::Engine {

/**
 * @brief Read the usage a texture recipe's `usage` key names.
 *
 * An absent key reads as Data. An unknown usage reads as Data too, through
 * reportError.
 *
 * @param source JSON source descriptor for a texture.
 * @return The usage the key names, or TextureUsage::Data.
 */
TextureUsage textureUsageFromRecipe(const nlohmann::json& source);

/**
 * @brief Report a texture recipe's value under @p key that none of its spellings match.
 *
 * Names the recipe by its path: a misspelt value shows only as a wrong look.
 *
 * @param source The texture's source descriptor.
 * @param key    The key whose value is unknown.
 * @param known  The spellings @p key takes, comma-separated.
 * @param readAs What the value reads as instead.
 */
void reportUnknownTextureValue(
    const nlohmann::json& source,
    const char* key,
    const std::string& known,
    const char* readAs
);

/**
 * @brief Read the filter override a texture recipe's optional `filter` key names.
 *
 * fileTextureRecipe writes the key only for a texture that has an opinion about
 * its own sampling, so an absent value reads as None and the texture follows the
 * scene's filtering setting. An unknown one reads as None too, through
 * reportUnknownTextureValue.
 *
 * @param source JSON source descriptor for a texture.
 * @return TextureFilterOverride::Nearest for "nearest", None otherwise.
 */
TextureFilterOverride textureFilterFromRecipe(const nlohmann::json& source);

/**
 * @brief Read the wrap mode a texture recipe's optional `wrap` key names.
 *
 * fileTextureRecipe writes the key only for a texture that does not clamp, so
 * an absent value reads as ClampToEdge. An unknown one reads as ClampToEdge too,
 * through reportUnknownTextureValue.
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

} // namespace Vkm::Engine
