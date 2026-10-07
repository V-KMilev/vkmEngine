#pragma once

#include <string>
#include <type_traits>

#include <nlohmann/json_fwd.hpp>

#include "core/reflect.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
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
 * (registerRecipeAssetFactories). A material has no slot: its recipe is its runtime form, which io
 * reads itself.
 */
struct AssetFactory {
    RecipeImport<MeshAsset>          createMesh          = nullptr;
    RecipeImport<TextureAsset>       createTexture       = nullptr;
    RecipeImport<SkeletonAsset>      createSkeleton      = nullptr;
    RecipeImport<AnimationClipAsset> createAnimationClip = nullptr;
    RecipeImport<AudioClipAsset>     createAudioClip     = nullptr;
    TextureDecode                    decodeTexture       = nullptr;
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
    const AssetFactory& factory = assetFactory();
    if constexpr (std::is_same_v<Asset, MeshAsset>)               return factory.createMesh;
    else if constexpr (std::is_same_v<Asset, TextureAsset>)       return factory.createTexture;
    else if constexpr (std::is_same_v<Asset, SkeletonAsset>)      return factory.createSkeleton;
    else if constexpr (std::is_same_v<Asset, AnimationClipAsset>) return factory.createAnimationClip;
    else if constexpr (std::is_same_v<Asset, AudioClipAsset>)     return factory.createAudioClip;
    else static_assert(Reflect::DEPENDENT_FALSE<Asset>, "recipeImport: this kind has no import slot");
}

} // namespace Vkm::Engine
