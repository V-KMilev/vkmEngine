#pragma once

// Built into vkm_pipeline, not vkm_loader: only a host that links the importers can call this.

namespace Vkm::Engine {

/**
 * @brief Wire the recipe imports into the AssetFactory seam.
 *
 * An import builds an asset from its source art when no cooked file serves it. Call once, before any
 * scene I/O.
 */
void registerRecipeAssetFactories();

} // namespace Vkm::Engine
