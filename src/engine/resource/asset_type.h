#pragma once

#include <cstddef>
#include <cstdint>

#include "base/reflect/enum_names.h"

namespace Vkm::Engine {

/**
 * @brief The asset kinds, once, as (tag, C++ type, serialized name, library directory, cooked kind).
 *
 * The directory is also the scene assets block's section key and the kind's list in
 * ComponentSerializer::AssetRefs; the cooked kind is the tag a cooked file's header carries.
 * Nothing on disk stores an enumerator's value, so rows may be reordered; the name, the directory
 * and the cooked kind are the format. Two kinds are not cooked, so their rows are written with NO_COOK
 * and their cooked kind is 0: a material, whose recipe is its runtime form, and a terrain, whose
 * .terrain file is, read as it is on every host.
 *
 * A new row fails the build until it has its place in ASSET_DEPENDENCY_ORDER below and, if it cooks,
 * its import installed by pipeline/import/recipe_registration.cpp, its `loadCooked`
 * (io/asset/cooked/cooked_loader.cpp) and the cooker's `writeCooked` and `hasContent` overloads.
 */
#define VKM_ASSET_ROWS(COOK, NO_COOK)                                         \
    COOK(Mesh,             MeshAsset,          "mesh",          meshes,    1) \
    COOK(Texture,          TextureAsset,       "texture",       textures,  2) \
    NO_COOK(Material,      MaterialAsset,      "material",      materials, 0) \
    COOK(Skeleton,         SkeletonAsset,      "skeleton",      skeletons, 3) \
    COOK(AnimationClip,    AnimationClipAsset, "animationClip", clips,     4) \
    COOK(AudioClip,        AudioClipAsset,     "audioClip",     sounds,    5) \
    NO_COOK(Terrain,       TerrainAsset,       "terrain",       terrains,  0)

/**
 * @brief Every asset kind, as X(tag, type, name, dir, cooked): each row of VKM_ASSET_ROWS.
 */
#define VKM_ASSET_KINDS(X) VKM_ASSET_ROWS(X, X)

/**
 * @brief The kinds that cook to a binary, as X(tag, type, name, dir, cooked): every row but the material's
 *        and the terrain's.
 */
#define VKM_COOKED_ASSET_KINDS(X) VKM_ASSET_ROWS(X, VKM_ASSET_NOT_COOKED)
#define VKM_ASSET_NOT_COOKED(tag, type, name, dir, cooked)

/**
 * @brief The asset kinds that live in the library, one enumerator per row of VKM_ASSET_KINDS.
 *
 * Here rather than with AssetLibrary, so code needing only the tag does not
 * pull in the manifest.
 */
#define VKM_ASSET_ENUMERATOR(tag, type, name, dir, cooked) tag,
enum class AssetType : uint8_t {
    VKM_ASSET_KINDS(VKM_ASSET_ENUMERATOR)

    Count
};
#undef VKM_ASSET_ENUMERATOR

#define VKM_ASSET_FORWARD_DECL(tag, type, name, dir, cooked) struct type;
VKM_ASSET_KINDS(VKM_ASSET_FORWARD_DECL)
#undef VKM_ASSET_FORWARD_DECL

inline constexpr AssetType ASSET_DEPENDENCY_ORDER[] = {
    AssetType::Texture,
    AssetType::Material,
    AssetType::Skeleton,
    AssetType::AnimationClip,
    AssetType::Mesh,
    AssetType::AudioClip,
    AssetType::Terrain,
};  ///< The order the kinds load and cook in, each after the kinds it names. Textures before the materials
    ///< that resolve them by name; rigs before the clips whose bone indices address them and the meshes
    ///< skinned to them; materials and meshes before the terrains whose layers and plants name them.

namespace detail {

constexpr bool everyKindOrderedOnce() {
    for (size_t kind = 0; kind < static_cast<size_t>(AssetType::Count); ++kind) {
        size_t seen = 0;
        for (const AssetType type : ASSET_DEPENDENCY_ORDER) seen += static_cast<size_t>(type) == kind ? 1 : 0;
        if (seen != 1) return false;
    }
    return sizeof(ASSET_DEPENDENCY_ORDER) / sizeof(AssetType) == static_cast<size_t>(AssetType::Count);
}
static_assert(everyKindOrderedOnce(), "each VKM_ASSET_KINDS row needs one place in ASSET_DEPENDENCY_ORDER");

} // namespace detail

/**
 * @brief The AssetType an asset struct is filed under, or Count for one that is not.
 *
 * Count for FontAsset, which the library does not hold; a static_assert where
 * the trait is used turns that into a readable error.
 */
template<typename Asset>
inline constexpr AssetType ASSET_TYPE = AssetType::Count;

#define VKM_ASSET_TRAIT(tag, type, name, dir, cooked) \
    template<> inline constexpr AssetType ASSET_TYPE<type> = AssetType::tag;
VKM_ASSET_KINDS(VKM_ASSET_TRAIT)
#undef VKM_ASSET_TRAIT

/**
 * @brief The tag a cooked file of @p Asset carries in its header, or 0 for a kind with no cooked file.
 */
template<typename Asset>
inline constexpr uint16_t COOKED_KIND = 0;

#define VKM_COOKED_KIND_TRAIT(tag, type, name, dir, cooked) \
    template<> inline constexpr uint16_t COOKED_KIND<type> = cooked;
VKM_COOKED_ASSET_KINDS(VKM_COOKED_KIND_TRAIT)
#undef VKM_COOKED_KIND_TRAIT

/**
 * @brief The tag a cooked file of @p type carries, or 0 for a kind with no cooked file.
 */
constexpr uint16_t cookedKindOf(AssetType type) {
    switch (type) {
#define VKM_COOKED_KIND_OF(tag, Asset, name, dir, cooked) case AssetType::tag: return COOKED_KIND<Asset>;
        VKM_ASSET_KINDS(VKM_COOKED_KIND_OF)
#undef VKM_COOKED_KIND_OF
        case AssetType::Count: break;
    }
    return 0;
}

} // namespace Vkm::Engine

#define VKM_ASSET_NAME(tag, type, name, dir, cooked) name,
VKM_ENUM_NAMES(::Vkm::Engine::AssetType, VKM_ASSET_KINDS(VKM_ASSET_NAME))
#undef VKM_ASSET_NAME
