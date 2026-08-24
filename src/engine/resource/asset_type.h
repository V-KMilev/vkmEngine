#pragma once

#include <cstdint>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The asset kinds that live in the library (shaders stay source-referenced and
 * are not part of the cooked database).
 *
 * The vocabulary rather than the database: this names what kinds of asset a
 * project holds, and AssetLibrary is one user of it. Keeping it here, beside
 * the assets it names, is what lets code that needs only the tag say which kind
 * without pulling in the manifest, its map and <filesystem> with it.
 *
 * Append new kinds before Count. Nothing on disk carries the numeric value -
 * the manifest and the scene write the name, and a cooked file carries its own
 * kind tag - but TYPE_DIRS in asset_library.cpp is indexed by it, and a
 * static_assert there ties the two together.
 */
enum class AssetType : uint8_t {
    Mesh,
    Texture,
    Material,
    Skeleton,
    AnimationClip,
    AudioClip,

    Count
};

struct AnimationClipAsset;
struct AudioClipAsset;
struct MaterialAsset;
struct MeshAsset;
struct SkeletonAsset;
struct TextureAsset;

/**
 * @brief The AssetType an asset struct is filed under, or Count for one that is not.
 *
 * The single place a C++ asset type is tied to its kind tag, so that code
 * holding only the type - an authored AssetRef<Asset> - can name the section of
 * the assets block, the manifest rows and the picker list it means without a
 * switch of its own. Count is the honest answer for FontAsset, which the
 * library does not hold, and a static_assert where the trait is used turns that
 * into a readable error rather than an out-of-range enum.
 */
template<typename Asset>
inline constexpr AssetType ASSET_TYPE = AssetType::Count;

template<> inline constexpr AssetType ASSET_TYPE<MeshAsset>          = AssetType::Mesh;
template<> inline constexpr AssetType ASSET_TYPE<TextureAsset>       = AssetType::Texture;
template<> inline constexpr AssetType ASSET_TYPE<MaterialAsset>      = AssetType::Material;
template<> inline constexpr AssetType ASSET_TYPE<SkeletonAsset>      = AssetType::Skeleton;
template<> inline constexpr AssetType ASSET_TYPE<AnimationClipAsset> = AssetType::AnimationClip;
template<> inline constexpr AssetType ASSET_TYPE<AudioClipAsset>     = AssetType::AudioClip;

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::AssetType, "mesh", "texture", "material", "skeleton", "animationClip", "audioClip")
