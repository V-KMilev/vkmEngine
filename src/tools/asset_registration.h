#pragma once

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Engine {

class ResourceManager;

/**
 * @brief The cooked dispatch (runtime + editor). Kinds that load from the cooked
 * asset database with no Assimp and no image decode: cooked meshes/textures and
 * inline materials (loaded from the library). Each switches internally on the
 * source `kind`. The editor's recipe dispatch falls through to these for any
 * kind it does not itself handle.
 */
MeshHandle     createCookedMesh    (const nlohmann::json& source, ResourceManager& resources);
TextureHandle  createCookedTexture (const nlohmann::json& source, ResourceManager& resources);
MaterialHandle createCookedMaterial(const nlohmann::json& source, ResourceManager& resources);

/**
 * @brief Runtime + editor. Wire the cooked dispatch into the AssetFactory seam.
 * Call once at startup before any scene I/O.
 */
void registerCookedAssetFactories();

/**
 * @brief Editor only. The heavy recipe kinds that (re)produce assets from their
 * source: procedural mesh generators, Assimp model import, and file/solid/
 * folder textures and materials. These are what the cooker runs to populate
 * the cooked cache; a runtime build does not link them. Call after
 * registerCookedAssetFactories().
 */
void registerRecipeAssetFactories();

} // namespace Engine
