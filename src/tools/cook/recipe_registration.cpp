#define VKM_LOG_CATEGORY "ASSETS"

#include "asset_registration.h"

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/asset_factory.h"
#include "io/asset/asset_serializer.h"
#include "resource/resource_manager.h"
#include "system/async/async_loader_system.h"
#include "generator/material_generators.h"
#include "generator/mesh_generators.h"
#include "generator/texture_generators.h"
#include "loader/material_loaders.h"
#include "loader/texture_loaders.h"
#include "loader/model_loaders.h"

namespace Vkm::Engine {

namespace {

MeshHandle createRecipeMesh(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});

    // Synchronous: generators are cheap, no benefit to async.
    if (kind == "generator") {
        const std::string type = source.value("type", std::string{});
        const auto& p = source.contains("params") ? source["params"] : nlohmann::json::object();

        MeshAsset mesh;
        if      (type == "cube")     mesh = generateCube();
        else if (type == "sphere")   mesh = generateSphere(p.value("xSegments", 32u),
                                                           p.value("ySegments", 16u));
        else if (type == "cone")     mesh = generateCone(p.value("radius",   0.5f),
                                                         p.value("height",   1.0f),
                                                         p.value("segments", 16u));
        else if (type == "pyramid")  mesh = generatePyramid(p.value("baseSize", 1.0f),
                                                            p.value("height",   1.0f));
        else if (type == "plane")    mesh = generatePlane(p.value("width",          1.0f),
                                                          p.value("height",         1.0f),
                                                          p.value("widthSegments",  1u),
                                                          p.value("heightSegments", 1u));
        else if (type == "triangle") mesh = generateTriangle(p.value("size", 1.0f));
        else return {};

        if (mesh.vertices.empty()) return {};
        return resources.add(std::move(mesh));
    }

    // Async: Assimp parsing is the slow path - return a stub immediately,
    // worker decodes off-thread, AsyncLoaderSystem patches the live asset
    // with vertices + bounds 1+ frames out.
    if (kind == "model") {
        return requestModelMeshAsync(source.value("path", std::string{}),
                                     source.value("mesh", -1),
                                     resources);
    }

    // The base (referenced by name) is emitted earlier in the meshes block - it
    // is the entity's Mesh::mesh - so a handle for it exists by the time this
    // runs. Re-decimating on load keeps decimated levels out of the scene file
    // (only the recipe is stored), matching how procedural generator meshes work.
    if (kind == "decimate") {
        const std::string baseName = source.value("base", std::string{});
        const uint32_t grid        = source.value("grid", 8u);
        const MeshHandle baseH = baseName.empty() ? MeshHandle{}
                                                  : resources.findByName<MeshAsset>(baseName);
        if (!baseH) {
            LOG_ERROR("decimate: base mesh '%s' not loaded (LOD level dropped)", baseName.c_str());
            return {};
        }
        // Resident is not the same as landed. A base that came from a model or
        // from the cooked cache decodes on the ThreadPool, and this runs inside
        // the scene load, before any frame that would finalise it - so the mesh
        // under that handle is an empty stub, decimating it yields nothing, and
        // the level disappears with only a "could not be recreated" to show for
        // it. Wait for it here, where there is still something to do about it.
        if (!awaitAsyncLoads(resources)) {
            LOG_ERROR("decimate: base mesh '%s' never finished loading (LOD level dropped)",
                baseName.c_str());
            return {};
        }
        MeshAsset dec = decimateMesh(resources.get(baseH), grid);
        if (dec.vertices.empty()) return {};
        // Keep the source so a subsequent save re-emits the recipe cleanly.
        dec.sourceJson() = {
            {"kind", "decimate"},
            {"base", baseName},
            {"grid", grid},
        };
        return resources.add(std::move(dec));
    }

    return createCookedMesh(source, resources);
}

TextureHandle createRecipeTexture(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});

    if (kind == "file") {
        const std::string path  = source.value("path", std::string{});
        if (path.empty()) return {};
        const bool sRGB         = source.value("sRGB", false);
        const bool genMipmaps   = source.value("generateMipmaps", true);
        // Async: a stub handle comes back immediately, ThreadPool decodes the
        // pixels off the main thread, AsyncLoaderSystem finalises the asset 1-3
        // frames later. Material binding shows a 1x1 gray fallback in the gap.
        return requestTextureAsync(path, resources, sRGB, genMipmaps);
    }

    if (kind == "builtin") {
        const std::string type = source.value("type", std::string{});
        if (type == "white")  return generateWhiteTexture(resources);
        if (type == "black")  return generateBlackTexture(resources);
        if (type == "normal") return generateNormalTexture(resources);
        if (type == "gray")   return generateGrayTexture(resources);
        return {};
    }

    // A user-authored solid-color texture (the Material Editor's "Generate
    // texture"), re-created from the stored RGBA + colorspace.
    if (kind == "solid") {
        glm::vec4 color(1.0f);
        if (source.contains("color") && source["color"].is_array() && source["color"].size() >= 4) {
            const auto& c = source["color"];
            color = glm::vec4(c[0].get<float>(), c[1].get<float>(),
                              c[2].get<float>(), c[3].get<float>());
        }
        const bool srgb = source.value("srgb", false);
        return createSolidColorTexture(color, resources, srgb);
    }

    // The pixels live in the .glb/.fbx, not in the scene JSON, so cold-start
    // load reopens the model file and pulls the same texture out of it.
    if (kind == "model-image") {
        const std::string path = source.value("path", std::string{});
        const std::string ref  = source.value("ref",  std::string{});
        const bool        srgb = source.value("sRGB", false);
        if (path.empty() || ref.empty()) return {};
        return loadModelEmbeddedTexture(path, ref, srgb, resources);
    }

    return createCookedTexture(source, resources);
}

SkeletonHandle createRecipeSkeleton(const nlohmann::json& source, ResourceManager& resources) {
    if (source.value("kind", std::string{}) == "model") {
        return loadModelSkeleton(source.value("path", std::string{}), resources);
    }
    return createCookedSkeleton(source, resources);
}

AnimationClipHandle createRecipeAnimationClip(const nlohmann::json& source, ResourceManager& resources) {
    if (source.value("kind", std::string{}) == "model") {
        return loadModelAnimationClip(source.value("path", std::string{}),
                                      source.value("clip", -1), resources);
    }
    return createCookedAnimationClip(source, resources);
}

MaterialHandle createRecipeMaterial(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});

    // Rediscovers the folder's textures from disk.
    if (kind == "folder") {
        const std::string path = source.value("path", std::string{});
        if (path.empty()) return {};
        return loadMaterialFromFolder(path, resources);
    }

    if (kind == "default") {
        return generateDefaultMaterial(resources);
    }

    if (kind == "model") {
        return loadModelMaterial(source.value("path", std::string{}),
                                 source.value("material", -1), resources);
    }

    return createCookedMaterial(source, resources);
}

} // namespace

void registerRecipeAssetFactories() {
    LOG_INFO("Registering recipe asset factories (meshes: generator/model/decimate, "
             "textures: file/builtin/solid/model-image, materials: folder/default/model, "
             "skeletons + clips: model)");
    assetFactory().createMesh     = &createRecipeMesh;
    assetFactory().createTexture  = &createRecipeTexture;
    assetFactory().createMaterial = &createRecipeMaterial;

    assetFactory().createSkeleton      = &createRecipeSkeleton;
    assetFactory().createAnimationClip = &createRecipeAnimationClip;
}

} // namespace Vkm::Engine
