#pragma once

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Create a mesh from its cooked source descriptor (runtime + editor).
 *
 * The cooked dispatch loads from the cooked asset database with no Assimp and
 * no image decode: cooked meshes/textures and inline materials (loaded from
 * the library). Switches internally on the source `kind`. The editor's recipe
 * dispatch falls through to these for any kind it does not itself handle.
 *
 * @param source JSON source descriptor carrying the asset `kind` and name.
 * @param resources Resource manager the new mesh is added to.
 * @return Handle to the created mesh, or an invalid handle on failure.
 */
MeshHandle     createCookedMesh    (const nlohmann::json& source, ResourceManager& resources);

/**
 * @brief Create a texture from its cooked source descriptor (runtime + editor).
 *
 * Takes the same cooked-cache path as createCookedMesh, switching on the
 * source `kind`; the recipe dispatch falls through to it for unhandled kinds.
 *
 * @param source JSON source descriptor carrying the asset `kind` and name.
 * @param resources Resource manager the new texture is added to.
 * @return Handle to the created texture, or an invalid handle on failure.
 */
TextureHandle  createCookedTexture (const nlohmann::json& source, ResourceManager& resources);

/**
 * @brief Create a material from its cooked (inline) source descriptor.
 *
 * Loads the canonical inline material form (PBR scalars + texture refs by
 * name) from the library; texture refs resolve via findByName. The recipe
 * dispatch falls through to it for unhandled kinds.
 *
 * @param source JSON source descriptor carrying the asset `kind` and fields.
 * @param resources Resource manager the new material is added to.
 * @return Handle to the created material, or an invalid handle on failure.
 */
MaterialHandle createCookedMaterial(const nlohmann::json& source, ResourceManager& resources);

/**
 * @brief Wire the cooked dispatch into the AssetFactory seam (runtime + editor).
 *
 * Call once at startup before any scene I/O.
 */
void registerCookedAssetFactories();

/**
 * @brief Wire the heavy recipe asset factories into the AssetFactory seam (editor only).
 *
 * The recipe kinds (re)produce assets from their source: procedural mesh
 * generators, Assimp model import, and file/solid/folder textures and
 * materials. These are what the cooker runs to populate the cooked cache; a
 * runtime build does not link them. Call after registerCookedAssetFactories().
 */
void registerRecipeAssetFactories();

} // namespace Engine
