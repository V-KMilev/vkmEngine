#define VKM_LOG_CATEGORY "ASSETS"

#include "asset_registration.h"

#include "logger.h"

#include "io/asset_serializer.h"
#include "io/cooked_loader.h"
#include "resource/resource_manager.h"
#include "resource/asset/shader_asset.h"

namespace Engine {

void registerCookedAssetFactories() {
    auto& factories = AssetFactories::get();
    LOG_INFO("Registering cooked asset factories (mesh/texture: cooked, material: inline, shader: directory)");

    // Meshes/textures: "cooked" kind. Loaded from the binary cache by name,
    // resolved through AssetLibrary. Async file read + deserialize off the main
    // thread, finalised by AsyncLoaderSystem - no Assimp, no stb decode.
    factories.registerMesh("cooked", [](const nlohmann::json& desc,
                                        ResourceManager& resources) -> MeshHandle
    {
        return requestCookedMeshAsync(desc.value("name", std::string{}), resources);
    });

    factories.registerTexture("cooked", [](const nlohmann::json& desc,
                                           ResourceManager& resources) -> TextureHandle
    {
        return requestCookedTextureAsync(desc.value("name", std::string{}), resources);
    });

    // Materials: "inline" kind - the canonical material form (PBR scalars +
    // texture refs by name). This is what the library stores and the runtime
    // loads; it has no heavy deps. Texture refs resolve via findByName against
    // textures already loaded earlier in the assets block.
    factories.registerMaterial("inline", [](const nlohmann::json& desc,
                                            ResourceManager& resources) -> MaterialHandle
    {
        MaterialAsset mat;
        AssetSerializer::applyInline(desc, mat, resources);
        auto handle = resources.add(std::move(mat));
        // Keep the source on the asset so subsequent saves re-emit cleanly.
        resources.edit(handle).sourceJson() = desc;
        return handle;
    });

    // Shaders: "directory" kind. vkmGL's path-based shader loading. The
    // descriptor carries the directory + the sampler->slot bindings the shader
    // expects. GLShader applies the bindings after every (re)compile so hot
    // reload survives. Engine-owned; not part of the cooked asset database.
    factories.registerShader("directory", [](const nlohmann::json& desc) -> ShaderAsset {
        ShaderAsset asset;
        asset.path = desc.value("path", std::string{});
        if (desc.contains("samplerBindings") && desc["samplerBindings"].is_object()) {
            for (auto& [name, slot] : desc["samplerBindings"].items()) {
                asset.samplerBindings[name] = slot.get<int>();
            }
        }
        asset.variantAware = desc.value("variantAware", false);
        return asset;
    });
}

} // namespace Engine
