#pragma once

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief The io<->tools dispatch seam.
 *
 * tools wires these at startup; the runtime wires the cooked dispatch, the
 * editor wires the cooked+recipe dispatch. Each scene-load asset is recreated
 * by calling the matching function pointer, which switches internally on the
 * source `kind`.
 */
struct AssetFactory {
    MeshHandle     (*createMesh)    (const nlohmann::json&, ResourceManager&) = nullptr;
    TextureHandle  (*createTexture) (const nlohmann::json&, ResourceManager&) = nullptr;
    MaterialHandle (*createMaterial)(const nlohmann::json&, ResourceManager&) = nullptr;
};

AssetFactory& assetFactory();

} // namespace Vkm::Engine
