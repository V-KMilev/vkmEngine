#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief An authored reference to an asset, held as the name it is filed under.
 *
 * What a behavior field declares when it wants a clip, a mesh or a material:
 * `AssetRef<AudioClipAsset> footstep;`. It stores the name rather than a
 * Handle<Asset> because a name is the engine's serializable identity for an
 * asset, while a handle names a slot in one session's ResourceManager and
 * nothing at all in the next one.
 *
 * The type argument is not decoration, and a bare std::string is not the same
 * thing: the scene's assets block is built by walking what the scene
 * references, and a name that block never lists is a name the loader never
 * recreates. Asset is what tells that walk which section the name belongs in,
 * and the editor which list to offer.
 *
 * An empty name means "none". Resolving is the behavior's own job, once, in
 * onStart():
 *
 *   m_clip = context().resources->findByName<AudioClipAsset>(footstep.name);
 *
 * @tparam Asset The asset struct referenced; it needs an ASSET_TYPE
 *               (resource/asset_type.h) to be authorable.
 */
template<typename Asset>
struct AssetRef {
    using asset_t = Asset;

    std::string name;   ///< Asset name; empty for none.
};

/**
 * @brief True iff T is an AssetRef<Asset>.
 *
 * ReflectedBehavior dispatches on this to route the field to
 * BehaviorFieldVisitor::assetField, the way it routes an enum to enumField.
 */
template<typename T>
inline constexpr bool IS_ASSET_REF = false;

template<typename Asset>
inline constexpr bool IS_ASSET_REF<AssetRef<Asset>> = true;

} // namespace Vkm::Engine
