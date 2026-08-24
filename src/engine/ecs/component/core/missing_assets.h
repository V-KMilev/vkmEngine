#pragma once

#include <string>
#include <vector>

#include "resource/asset_type.h"

namespace Vkm::Engine {

/**
 * @brief One asset reference a scene load could not resolve.
 *
 * Addressed the way the scene file addresses it - the component's key and the
 * field's key within that component's block - because that is what a save has
 * to put it back into.
 */
struct MissingAssetRef {
    std::string component;   ///< Scene-format component key, e.g. "Mesh".
    std::string field;       ///< Key inside that component's block, e.g. "material".
    std::string name;        ///< The asset name the file gave, which nothing answered to.
    AssetType   type = AssetType::Count;  ///< Which kind it was looked up as.
};

/**
 * @brief The references on one entity that the load could not resolve, kept so
 * a later save writes them back rather than erasing them.
 *
 * An asset name resolves against the graph the load brought in, and a name it
 * cannot answer leaves the component's slot empty. That much is unavoidable -
 * the asset is not there - but the *name* was the author's work, and without it
 * a save would write an empty string over the only record of what belonged in
 * that slot. Opening a scene whose cooked library a teammate did not commit,
 * then saving out of habit, is how a whole level loses its meshes.
 *
 * Present only on entities that had such a reference, so a healthy scene
 * carries none of these. Never written to the scene file as a component of its
 * own: what it holds goes back into the field it came from, which is where the
 * next load will look for it, and into the document's assets block, which is
 * what tells that load to bring the asset in at all. Both halves are needed -
 * a field naming an asset the block does not declare stays unresolved even
 * once the library holding it is back.
 */
struct MissingAssets {
    std::vector<MissingAssetRef> refs;
};

} // namespace Vkm::Engine
