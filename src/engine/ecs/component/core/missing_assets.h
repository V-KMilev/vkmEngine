#pragma once

#include <string>
#include <vector>

#include "resource/asset_type.h"

namespace Vkm::Engine {

/**
 * @brief One asset reference a scene load could not resolve.
 *
 * Addressed by component and field key, as the scene file addresses it, because
 * that is where a save puts it back.
 */
struct MissingAssetRef {
    std::string component;   ///< Scene-format component key, e.g. "Mesh".
    std::string field;       ///< Key inside that component's block, e.g. "material".
    std::string name;        ///< The asset name the file gave.
    AssetType   type = AssetType::Count;  ///< Kind it was looked up as.
};

/**
 * @brief The references on one entity that the load could not resolve, kept so
 * a later save writes them back rather than erasing them.
 *
 * Present only on entities that had one. Never saved as a component: each name
 * goes back into its field and into the document's assets block - both, since a
 * field naming an asset the block does not declare stays unresolved even once
 * the library holding it is back.
 */
struct MissingAssets {
    std::vector<MissingAssetRef> refs;
};

} // namespace Vkm::Engine
