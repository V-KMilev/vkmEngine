#define VKM_LOG_CATEGORY "ASSETS"

#include "asset_registration.h"

#include "logger.h"

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
    LOG_INFO("Registering builtin factories (meshes: generator/model, textures: file/builtin/model-image, materials: folder/default/inline/model, shaders: directory)");

    // Meshes: "generator" kind. type selects which procedural shape.
    // Synchronous: generators are cheap, no benefit to async.
    factories.registerMesh("generator", [](const nlohmann::json& desc,
                                           ResourceManager& resources) -> MeshHandle
    {
        const std::string type = desc.value("type", std::string{});
        const auto& p = desc.contains("params") ? desc["params"] : nlohmann::json::object();

        MeshAsset mesh;
        if      (type == "cube")     mesh = generateCube();
        else if (type == "sphere")   mesh = generateSphere(p.value("xSegments", 32u),
                                                           p.value("ySegments", 16u));
        else if (type == "cone")     mesh = generateCone(p.value("radius",   0.5f),
                                                         p.value("height",   1.0f),
                                                         p.value("segments", 16u));
        else if (type == "pyramid")  mesh = generatePyramid(p.value("baseSize", 2.0f),
                                                            p.value("height",   2.0f));
        else if (type == "plane")    mesh = generatePlane(p.value("width",          1.0f),
                                                          p.value("height",         1.0f),
                                                          p.value("widthSegments",  1u),
                                                          p.value("heightSegments", 1u));
        else if (type == "triangle") mesh = generateTriangle(p.value("size", 2.0f));
        else return {};

        if (mesh.vertices.empty()) return {};
        return resources.add(std::move(mesh));
    });

    // Meshes: "model" kind. One aiMesh re-imported via Assimp.
    // Async: Assimp parsing is the slow path - return a stub immediately,
    // worker decodes off-thread, AsyncLoaderSystem patches the live asset
    // with vertices + bounds 1+ frames out.
    factories.registerMesh("model", [](const nlohmann::json& desc,
                                       ResourceManager& resources) -> MeshHandle
    {
        return requestModelMeshAsync(desc.value("path", std::string{}),
                                     desc.value("mesh", -1),
                                     resources);
    });

    // Meshes: "decimate" kind. A LOD level produced by vertex-clustering its
    // base mesh. The base ({base AssetId}) is emitted earlier in the meshes
    // block (it is the entity's Mesh::mesh), so it is already resident when
    // this factory runs. Re-decimating on load keeps decimated levels out of
    // the scene file (only the recipe is stored), matching how procedural
    // generator meshes work.
    factories.registerMesh("decimate", [](const nlohmann::json& desc,
                                          ResourceManager& resources) -> MeshHandle
    {
        const AssetId baseId = AssetId::fromString(desc.value("base", std::string{}));
        const uint32_t grid  = desc.value("grid", 8u);
        const MeshHandle baseH = baseId ? resources.findById<MeshAsset>(baseId) : MeshHandle{};
        if (!baseH) {
            LOG_ERROR("decimate: base mesh %s not loaded (LOD level dropped)", baseId.toString().c_str());
            return {};
        }
        MeshAsset dec = decimateMesh(resources.get(baseH), grid);
        if (dec.vertices.empty()) return {};
        // Keep the source so a subsequent save re-emits the recipe cleanly
        // (loadAssets::reconcileId patches the assetId to the scene's GUID).
        nlohmann::json src;
        src["kind"] = "decimate";
        src["base"] = baseId.toString();
        src["grid"] = grid;
        dec.sourceJson() = std::move(src);
        return resources.add(std::move(dec));
    });

    // Textures: "file" kind. Loaded via stb_image.
    factories.registerTexture("file", [](const nlohmann::json& desc,
                                         ResourceManager& resources) -> TextureHandle
    {
        const std::string path  = desc.value("path", std::string{});
        if (path.empty()) return {};
        const bool sRGB         = desc.value("sRGB", false);
        const bool genMipmaps   = desc.value("generateMipmaps", true);
        // Scene-load file textures take the async path: returns immediately
        // with a stub handle, ThreadPool decodes the pixels off the main
        // thread, AsyncLoaderSystem finalises the asset 1-3 frames later.
        // Material binding shows a 1x1 gray fallback in the gap.
        return requestTextureAsync(path, resources, sRGB, genMipmaps);
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

    // Textures: "solid" kind. A user-authored solid-color texture (Material
    // Editor "Generate texture"). Re-created from the stored RGBA + colorspace.
    factories.registerTexture("solid", [](const nlohmann::json& desc,
                                          ResourceManager& resources) -> TextureHandle
    {
        glm::vec4 color(1.0f);
        if (desc.contains("color") && desc["color"].is_array() && desc["color"].size() >= 4) {
            const auto& c = desc["color"];
            color = glm::vec4(c[0].get<float>(), c[1].get<float>(),
                              c[2].get<float>(), c[3].get<float>());
        }
        const bool srgb = desc.value("srgb", false);
        return createSolidColorTexture(color, resources, srgb);
    });

    // Textures: "model-image" kind. Re-extract an embedded image from the
    // source model file. The pixels live in the .glb/.fbx, not in the scene
    // JSON, so cold-start load reopens the file and pulls the same texture.
    factories.registerTexture("model-image", [](const nlohmann::json& desc,
                                                ResourceManager& resources) -> TextureHandle
    {
        const std::string path = desc.value("path", std::string{});
        const std::string ref  = desc.value("ref",  std::string{});
        const bool        srgb = desc.value("sRGB", false);
        if (path.empty() || ref.empty()) return {};
        return loadModelEmbeddedTexture(path, ref, srgb, resources);
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
