#define VKM_LOG_CATEGORY "ASSETS"

#include "asset_registration.h"

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/asset_factory.h"
#include "io/asset/asset_serializer.h"
#include "io/asset/cooked_loader.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

// Async file read + deserialize off the main thread, finalised by
// AsyncLoaderSystem.
MeshHandle createCookedMesh(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "cooked") {
        return requestCookedMeshAsync(source.value("name", std::string{}), resources);
    }
    LOG_ERROR("No cooked mesh dispatch for kind '%s'", kind.c_str());
    return {};
}

TextureHandle createCookedTexture(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "cooked") {
        return requestCookedTextureAsync(source.value("name", std::string{}), resources);
    }
    LOG_ERROR("No cooked texture dispatch for kind '%s'", kind.c_str());
    return {};
}

// Texture refs resolve via findByName against textures already loaded earlier
// in the assets block.
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

SkeletonHandle createCookedSkeleton(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "cooked") {
        return loadCookedSkeleton(source.value("name", std::string{}), resources);
    }
    LOG_ERROR("No cooked skeleton dispatch for kind '%s'", kind.c_str());
    return {};
}

AnimationClipHandle createCookedAnimationClip(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});
    if (kind == "cooked") {
        return loadCookedAnimationClip(source.value("name", std::string{}), resources);
    }
    LOG_ERROR("No cooked clip dispatch for kind '%s'", kind.c_str());
    return {};
}

void registerCookedAssetFactories() {
    LOG_INFO("Registering cooked asset factories (mesh/texture/skeleton/clip: cooked, material: inline)");
    assetFactory().createMesh          = &createCookedMesh;
    assetFactory().createTexture       = &createCookedTexture;
    assetFactory().createMaterial      = &createCookedMaterial;
    assetFactory().createSkeleton      = &createCookedSkeleton;
    assetFactory().createAnimationClip = &createCookedAnimationClip;
}

} // namespace Vkm::Engine
