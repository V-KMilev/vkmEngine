#pragma once

#include <array>
#include <string>

#include <nlohmann/json.hpp>

#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Texture fields on MaterialAsset, paired with their stable JSON key.
 */
struct TexField {
    const char*                    key;
    TextureHandle MaterialAsset::* member;
};
// Sized from the macro, not a number beside it: a removed map would leave a {nullptr, nullptr} row the
// walks over it take as a real slot.
#define VKM_MATERIAL_MAP_FIELD(key, member, slot, doc) {#key, &MaterialAsset::member},
inline constexpr std::array<TexField, MATERIAL_MAP_COUNT> MATERIAL_TEXTURE_FIELDS = {{
    VKM_MATERIAL_MAPS(VKM_MATERIAL_MAP_FIELD)
}};
#undef VKM_MATERIAL_MAP_FIELD

/**
 * @brief Build a material from its recipe.
 *
 * A missing key reads as the field's default, and one no field has is warned of and dropped; a
 * wrong-typed value throws, for the caller to report against the material.
 *
 * @param source    The recipe, as materialToRecipe writes it.
 * @param name      The material's name, for what is logged.
 * @param resources Resolves texture names; a name it does not hold leaves that map empty, logged.
 * @return The material, unnamed.
 */
MaterialAsset materialFromRecipe(
    const nlohmann::json& source,
    const std::string& name,
    const ResourceManager& resources
);

/**
 * @brief Build a material's recipe: its PBR scalars plus texture refs by name.
 *
 * The material's editable source of truth: what its recipe file holds.
 *
 * @param material  The material to describe.
 * @param resources Resolves its texture handles to the names written.
 * @return The recipe.
 */
nlohmann::json materialToRecipe(const MaterialAsset& material, const ResourceManager& resources);

} // namespace Vkm::Engine
