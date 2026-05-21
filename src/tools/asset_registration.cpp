#include "asset_registration.h"

#include "io/asset_serializer.h"
#include "resource/resource_manager.h"
#include "resource/shader_asset.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"
#include "generator/texture_generators.h"
#include "loader/material_loaders.h"
#include "loader/texture_loaders.h"
#include "loader/model_loader.h"

namespace Engine {

void registerBuiltinAssetFactories() {
    auto& factories = AssetFactories::get();

    // Meshes: "generator" kind. type selects which procedural shape.
    factories.registerMesh("generator", [](const nlohmann::json& desc) -> MeshAsset {
        const std::string type = desc.value("type", std::string{});
        const auto& p = desc.contains("params") ? desc["params"] : nlohmann::json::object();

        if (type == "cube")     return generateCube();
        if (type == "sphere")   return generateSphere(p.value("xSegments", 32u),
                                                      p.value("ySegments", 16u));
        if (type == "cone")     return generateCone(p.value("radius",   0.5f),
                                                    p.value("height",   1.0f),
                                                    p.value("segments", 16u));
        if (type == "pyramid")  return generatePyramid(p.value("baseSize", 2.0f),
                                                       p.value("height",   2.0f));
        if (type == "plane")    return generatePlane(p.value("width",          1.0f),
                                                     p.value("height",         1.0f),
                                                     p.value("widthSegments",  1u),
                                                     p.value("heightSegments", 1u));
        if (type == "triangle") return generateTriangle(p.value("size", 2.0f));

        return {};
    });

    // Meshes: "model" kind. One aiMesh re-imported via Assimp.
    factories.registerMesh("model", [](const nlohmann::json& desc) -> MeshAsset {
        return loadModelMesh(desc.value("path", std::string{}),
                             desc.value("mesh", -1));
    });

    // Textures: "file" kind. Loaded via stb_image.
    factories.registerTexture("file", [](const nlohmann::json& desc,
                                         ResourceManager& resources) -> TextureHandle
    {
        const std::string path  = desc.value("path", std::string{});
        if (path.empty()) return {};
        const bool sRGB         = desc.value("sRGB", false);
        const bool genMipmaps   = desc.value("generateMipmaps", true);
        return loadTexture(path, resources, sRGB, genMipmaps);
    });

    // Textures: "builtin" kind. 1x1 default textures (white/black/normal/gray).
    factories.registerTexture("builtin", [](const nlohmann::json& desc,
                                            ResourceManager& resources) -> TextureHandle
    {
        const std::string type = desc.value("type", std::string{});
        if (type == "white")  return generateWhiteTexture(resources);
        if (type == "black")  return generateBlackTexture(resources);
        if (type == "normal") return generateNormalTexture(resources);
        if (type == "gray")   return generateGrayTexture(resources);
        return {};
    });

    // Materials: "folder" kind. Rediscovers textures from disk.
    factories.registerMaterial("folder", [](const nlohmann::json& desc,
                                            ResourceManager& resources) -> MaterialHandle
    {
        const std::string path = desc.value("path", std::string{});
        if (path.empty()) return {};
        return loadMaterialFromFolder(path, resources);
    });

    // Materials: "default" kind. A neutral white PBR material.
    factories.registerMaterial("default", [](const nlohmann::json&,
                                             ResourceManager& resources) -> MaterialHandle
    {
        return generateDefaultMaterial(resources);
    });

    // Materials: "inline" kind. Full PBR descriptor with texture refs - the
    // canonical save format. Texture refs resolve via findByName<TextureAsset>
    // against textures already loaded earlier in the assets block.
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

    // Materials: "model" kind. Re-imported with textures via Assimp.
    factories.registerMaterial("model", [](const nlohmann::json& desc,
                                           ResourceManager& resources) -> MaterialHandle
    {
        return loadModelMaterial(desc.value("path", std::string{}),
                                 desc.value("material", -1), resources);
    });

    // Shaders: "directory" kind. vkmGL's path-based shader loading. The
    // descriptor carries the directory + the sampler->slot bindings the
    // shader expects. GLShader applies the bindings after every (re)compile
    // so hot reload survives without each pass re-asserting them.
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
