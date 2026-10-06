#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief An authored reference to an asset, held as the name it is filed under.
 *
 * A behavior field such as `AssetRef<AudioClipAsset> footstep;`. A name, not a
 * Handle<Asset>, because a handle means nothing in the next session. The type
 * tells AssetSerializer's walk which section of the assets block the name goes
 * in, and the editor which list to offer.
 *
 * An empty name means "none". The behavior resolves it once, in onStart():
 *
 *   m_clip = resources().find(footstep);
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
 * Routes a reflected field to BehaviorFieldVisitor::assetField.
 */
template<typename T>
inline constexpr bool IS_ASSET_REF = false;

template<typename Asset>
inline constexpr bool IS_ASSET_REF<AssetRef<Asset>> = true;

} // namespace Vkm::Engine
