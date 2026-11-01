#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

#include "base/reflect/reflect.h"
#include "resource/asset_type.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Builds an asset from its recipe by re-running the import.
 *
 * @tparam Asset The kind it builds.
 */
template<typename Asset>
using RecipeImport = Handle<Asset> (*)(const nlohmann::json&, ResourceManager&);

/**
 * @brief Decodes a texture file into @p out on the calling thread, outside any ResourceManager.
 *
 * For a bake that reads one texture to make another: a roughness map's paired normal map.
 *
 * @param ref   The file, project-relative.
 * @param usage What its texels mean, which picks the decoded format.
 * @param out   The decoded texture.
 * @return False when the file did not decode.
 */
using TextureDecode = bool (*)(const std::string& ref, TextureUsage usage, TextureAsset& out);

/**
 * @brief The io<->tools import seam: one recipe import per kind that cooks to a binary, and
 * the texture decode a bake reads a second texture by.
 *
 * io loads cooked files itself but cannot link the importers, so a host that imports installs these
 * (registerRecipeAssetFactories). Each kind of VKM_COOKED_ASSET_KINDS has its slot, `create` and its
 * tag: createMesh, createTexture and the rest. A material has none: its recipe is its runtime form,
 * which io reads itself.
 */
struct AssetFactory {
#define VKM_ASSET_IMPORT_SLOT(tag, Asset, name, dir, cooked) RecipeImport<Asset> create##tag = nullptr;
    VKM_COOKED_ASSET_KINDS(VKM_ASSET_IMPORT_SLOT)
#undef VKM_ASSET_IMPORT_SLOT

    TextureDecode decodeTexture = nullptr;
};

/**
 * @brief The process-wide factory table, empty until a host wires it.
 *
 * @return The one table every recipe import dispatches through.
 */
AssetFactory& assetFactory();

/**
 * @brief The slot of assetFactory() that imports @p Asset.
 *
 * @tparam Asset A kind with a slot; any other is a compile error.
 * @return The slot's import, null when no host installed one.
 */
template<typename Asset>
RecipeImport<Asset> recipeImport() {
    static_assert(Reflect::DEPENDENT_FALSE<Asset>, "recipeImport: this kind has no import slot");
    return nullptr;
}

#define VKM_ASSET_RECIPE_IMPORT(tag, Asset, name, dir, cooked) \
    template<> inline RecipeImport<Asset> recipeImport<Asset>() { return assetFactory().create##tag; }
VKM_COOKED_ASSET_KINDS(VKM_ASSET_RECIPE_IMPORT)
#undef VKM_ASSET_RECIPE_IMPORT

} // namespace Vkm::Engine
