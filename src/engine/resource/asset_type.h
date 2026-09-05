#pragma once

#include <cstdint>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The asset kinds, once, as (tag, C++ type, serialized name, library directory).
 *
 * Six kinds in one place. The enum, the ASSET_TYPE specialisations, the
 * VKM_ENUM_NAMES table, the forward declarations and TYPE_DIRS in
 * asset_library.cpp all expand from here, so none of them can drift from the
 * others and none needs a static_assert to notice if it had.
 *
 * Adding a kind is a row here. Nothing on disk carries the numeric value: the
 * manifest and the scene write the name, and a cooked file carries its own kind
 * tag, so a row may be appended freely and only reordering is a format change.
 */
#define VKM_ASSET_KINDS(X)                                              \
    X(Mesh,          MeshAsset,          "mesh",          "meshes")     \
    X(Texture,       TextureAsset,       "texture",       "textures")   \
    X(Material,      MaterialAsset,      "material",      "materials")  \
    X(Skeleton,      SkeletonAsset,      "skeleton",      "skeletons")  \
    X(AnimationClip, AnimationClipAsset, "animationClip", "clips")      \
    X(AudioClip,     AudioClipAsset,     "audioClip",     "sounds")

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
#define VKM_ASSET_ENUMERATOR(tag, type, name, dir) tag,
enum class AssetType : uint8_t {
    VKM_ASSET_KINDS(VKM_ASSET_ENUMERATOR)

    Count
};
#undef VKM_ASSET_ENUMERATOR

#define VKM_ASSET_FORWARD_DECL(tag, type, name, dir) struct type;
VKM_ASSET_KINDS(VKM_ASSET_FORWARD_DECL)
#undef VKM_ASSET_FORWARD_DECL

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

#define VKM_ASSET_TRAIT(tag, type, name, dir) \
    template<> inline constexpr AssetType ASSET_TYPE<type> = AssetType::tag;
VKM_ASSET_KINDS(VKM_ASSET_TRAIT)
#undef VKM_ASSET_TRAIT

} // namespace Vkm::Engine

#define VKM_ASSET_NAME(tag, type, name, dir) name,
VKM_ENUM_NAMES(::Vkm::Engine::AssetType, VKM_ASSET_KINDS(VKM_ASSET_NAME))
#undef VKM_ASSET_NAME
