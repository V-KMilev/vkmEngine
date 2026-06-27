#define VKM_LOG_CATEGORY "ASSETS"

#include "asset_registration.h"

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/asset_factory.h"
#include "io/asset/asset_serializer.h"
#include "io/asset/cooked_loader.h"
#include "resource/resource_manager.h"

namespace Engine {

// Meshes: "cooked" kind. Loaded from the binary cache by name, resolved through
// AssetLibrary. Async file read + deserialize off the main thread, finalised by
// AsyncLoaderSystem.
MeshHandle createCookedMesh(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "cooked") {
        return requestCookedMeshAsync(source.value("name", std::string{}), resources);
    }
    LOG_ERROR("No cooked mesh dispatch for kind '%s'", kind.c_str());
    return {};
}

// Textures: "cooked" kind. Same cooked-cache path as meshes.
TextureHandle createCookedTexture(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "cooked") {
        return requestCookedTextureAsync(source.value("name", std::string{}), resources);
    }
    LOG_ERROR("No cooked texture dispatch for kind '%s'", kind.c_str());
    return {};
}

// Materials: "inline" kind - the canonical material form (PBR scalars + texture
// refs by name). This is what the library stores and the runtime loads; it has
// no heavy deps. Texture refs resolve via findByName against textures already
// loaded earlier in the assets block.
MaterialHandle createCookedMaterial(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "inline") {
        MaterialAsset mat;
        AssetSerializer::applyInline(source, mat, resources);
        auto handle = resources.add(std::move(mat));
        // Keep the source on the asset so subsequent saves re-emit cleanly.
        resources.edit(handle).sourceJson() = source;
        return handle;
    }
    LOG_ERROR("No cooked material dispatch for kind '%s'", kind.c_str());
    return {};
}

void registerCookedAssetFactories() {
    LOG_INFO("Registering cooked asset factories (mesh/texture: cooked, material: inline)");
    assetFactory().createMesh     = &createCookedMesh;
    assetFactory().createTexture  = &createCookedTexture;
    assetFactory().createMaterial = &createCookedMaterial;
}

} // namespace Engine
