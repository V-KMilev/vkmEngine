#pragma once

#include <cstddef>
#include <cstdint>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The asset kinds, once, as (tag, C++ type, serialized name, library directory).
 *
 * The directory name is also the scene assets block's section key. A new kind
 * also needs a case in `cookedKind` (asset_cook.cpp), a place in
 * ASSET_DEPENDENCY_ORDER below, a loader in io/asset/cooked_loader.h and an
 * AssetFactory slot filled in tools/cook/recipe_registration.cpp. Nothing on disk
 * stores the numeric value, so rows may be reordered; the name and directory are
 * the format.
 */
#define VKM_ASSET_KINDS(X)                                              \
    X(Mesh,          MeshAsset,          "mesh",          "meshes")     \
    X(Texture,       TextureAsset,       "texture",       "textures")   \
    X(Material,      MaterialAsset,      "material",      "materials")  \
    X(Skeleton,      SkeletonAsset,      "skeleton",      "skeletons")  \
    X(AnimationClip, AnimationClipAsset, "animationClip", "clips")      \
    X(AudioClip,     AudioClipAsset,     "audioClip",     "sounds")

/**
 * @brief The asset kinds that live in the library, one enumerator per row of VKM_ASSET_KINDS.
 *
 * Here rather than with AssetLibrary, so code needing only the tag does not
 * pull in the manifest.
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
 * @brief The order the kinds load and cook in, each after the kinds it names.
 *
 * Textures before the materials that resolve them by name; rigs before the clips whose bone indices
 * address them and the meshes skinned to them.
 */
inline constexpr AssetType ASSET_DEPENDENCY_ORDER[] = {
    AssetType::Texture,
    AssetType::Material,
    AssetType::Skeleton,
    AssetType::AnimationClip,
    AssetType::Mesh,
    AssetType::AudioClip,
};

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

#define VKM_ASSET_TRAIT(tag, type, name, dir) \
    template<> inline constexpr AssetType ASSET_TYPE<type> = AssetType::tag;
VKM_ASSET_KINDS(VKM_ASSET_TRAIT)
#undef VKM_ASSET_TRAIT

} // namespace Vkm::Engine

#define VKM_ASSET_NAME(tag, type, name, dir) name,
VKM_ENUM_NAMES(::Vkm::Engine::AssetType, VKM_ASSET_KINDS(VKM_ASSET_NAME))
#undef VKM_ASSET_NAME
