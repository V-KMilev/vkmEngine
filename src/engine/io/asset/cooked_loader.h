#pragma once

#include <string>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Load cooked assets by their library name (the runtime load path).
 *
 * Resolves the cooked file through AssetLibrary, returns immediately with a
 * loading stub, and reads the binary off-thread on the ThreadPool; finalisation
 * happens on the main thread via AsyncLoaderSystem, exactly like the editor's
 * stb/Assimp loaders. No recipe factory, no Assimp, no stb.
 *
 * Idempotent: a name already resident returns its existing handle. Returns an
 * invalid handle if the name has no cooked entry in the manifest.
 */
MeshHandle    requestCookedMeshAsync   (const std::string& name, ResourceManager& resources);
TextureHandle requestCookedTextureAsync(const std::string& name, ResourceManager& resources);

} // namespace Engine
