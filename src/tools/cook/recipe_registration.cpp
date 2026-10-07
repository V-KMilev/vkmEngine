#define VKM_LOG_CATEGORY "ASSETS"

#include "cook/recipe_registration.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/asset_factory.h"
#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "system/async/async_loader_system.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/texture_generators.h"
#include "import/audio_loaders.h"
#include "import/texture_loaders.h"
#include "import/model_loaders.h"
#include "cook/lod_generator.h"
#include "cook/mesh_processing.h"
#include "resource/asset_source_kind.h"

namespace Vkm::Engine {

namespace {

// A recipe of a kind no import of its asset type reads.
template<typename Asset>
Handle<Asset> refuseKind(const char* what, const nlohmann::json& source) {
    LOG_ERROR("No %s import for recipe kind '%s'", what, source.value("kind", std::string{}).c_str());
    return {};
}

MeshHandle createRecipeMesh(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});

    // Synchronous: generators are cheap, no benefit to async.
    if (kind == AssetSourceKind::GENERATOR) return createGeneratedMesh(source, resources);

    // Async: parsing a model file is the slow path.
    if (kind == AssetSourceKind::MODEL) {
        return requestModelMeshAsync(
            source.value(AssetSourceKey::PATH, std::string{}),
            source.value(AssetSourceKey::MESH, -1),
            resources
        );
    }

    // The base must come earlier in the meshes block, or the level is dropped.
    if (kind == AssetSourceKind::DECIMATE) {
        const std::string baseName = source.value(AssetSourceKey::BASE, std::string{});
        const float ratio          = decimateRatioFromRecipe(source);
        const MeshHandle baseH = baseName.empty() ? MeshHandle{} : resources.findByName<MeshAsset>(baseName);
        if (!baseH) {
            LOG_ERROR("decimate: base mesh '%s' not loaded (LOD level dropped)", baseName.c_str());
            return {};
        }
        // Resident is not landed: the base decodes on the ThreadPool and no frame has finalised it
        // inside a scene load. Decimating a stub yields nothing.
        if (!awaitAsyncLoads(resources)) {
            LOG_ERROR(
                "decimate: base mesh '%s' never finished loading (LOD level dropped)",
                baseName.c_str()
            );
            return {};
        }
        MeshAsset dec = decimateMesh(resources.get(baseH), ratio);
        if (dec.vertices.empty()) return {};
        // So a later save re-emits the recipe.
        dec.sourceJson() = decimateRecipe(baseName, ratio);
        return resources.add(std::move(dec));
    }

    return refuseKind<MeshAsset>("mesh", source);
}

// A project-relative texture file, decoded whole on this thread.
bool decodeTextureFile(const std::string& ref, TextureUsage usage, TextureAsset& out) {
    std::ifstream file(ProjectPaths::resolveProjectPath(ref), std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
    return !bytes.empty() && decodeTextureFromMemory(bytes.data(), bytes.size(), usage, out);
}

TextureHandle createRecipeTexture(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value("kind", std::string{});

    if (kind == AssetSourceKind::FILE) {
        const std::string path = source.value(AssetSourceKey::PATH, std::string{});
        if (path.empty()) return {};
        const bool genMipmaps = textureMipmapsFromRecipe(source);
        const TextureHandle handle = requestTextureAsync(
            path,
            resources,
            textureUsageFromRecipe(source),
            genMipmaps,
            textureFilterFromRecipe(source),
            textureWrapFromRecipe(source)
        );
        // The request writes the recipe from what it decodes by; a pairing the import made rides
        // across, or a re-import would drop it and the cook would think nothing changed.
        if (handle && source.contains(AssetSourceKey::ROUGHNESS_NORMAL)) {
            resources.edit(handle).sourceJson()[AssetSourceKey::ROUGHNESS_NORMAL] =
                source[AssetSourceKey::ROUGHNESS_NORMAL];
        }
        return handle;
    }

    if (kind == AssetSourceKind::SOLID) {
        return createGeneratedTexture(source, resources);
    }

    // The pixels live in the .glb/.fbx, so a load reopens the model file.
    if (kind == AssetSourceKind::MODEL_IMAGE) {
        const std::string path = source.value(AssetSourceKey::PATH, std::string{});
        const std::string ref  = source.value(AssetSourceKey::REF, std::string{});
        if (path.empty() || ref.empty()) return {};
        return loadModelEmbeddedTexture(path, ref, textureUsageFromRecipe(source), resources);
    }

    return refuseKind<TextureAsset>("texture", source);
}

SkeletonHandle createRecipeSkeleton(const nlohmann::json& source, ResourceManager& resources) {
    if (source.value("kind", std::string{}) == AssetSourceKind::MODEL) {
        return loadModelSkeleton(source.value(AssetSourceKey::PATH, std::string{}), resources);
    }
    return refuseKind<SkeletonAsset>("skeleton", source);
}

// Authored clip markers, an array of {name, time}; glTF and FBX name no animation events. Hashed
// into the recipe, so adding a footstep re-cooks the clip.
std::vector<ClipMarker> recipeMarkers(const nlohmann::json& source) {
    std::vector<ClipMarker> markers;
    const auto it = source.find(AssetSourceKey::MARKERS);
    if (it == source.end() || !it->is_array()) return markers;

    markers.reserve(it->size());
    for (const nlohmann::json& entry : *it) {
        if (!entry.is_object()) continue;
        markers.push_back({entry.value("name", std::string{}), entry.value("time", 0.0f)});
    }
    return markers;
}

AnimationClipHandle createRecipeAnimationClip(const nlohmann::json& source, ResourceManager& resources) {
    if (source.value("kind", std::string{}) == AssetSourceKind::MODEL) {
        return loadModelAnimationClip(
            source.value(AssetSourceKey::PATH, std::string{}),
            source.value(AssetSourceKey::CLIP, -1),
            recipeMarkers(source),
            resources,
            source.value(AssetSourceKey::RIG, std::string{})
        );
    }
    return refuseKind<AnimationClipAsset>("clip", source);
}

AudioClipHandle createRecipeAudioClip(const nlohmann::json& source, ResourceManager& resources) {
    if (source.value("kind", std::string{}) == AssetSourceKind::FILE) {
        return loadAudioClip(source.value(AssetSourceKey::PATH, std::string{}), resources);
    }
    return refuseKind<AudioClipAsset>("sound", source);
}

} // namespace

void registerRecipeAssetFactories() {
    LOG_INFO(
        "Registering recipe asset factories (meshes: generator/model/decimate, "
        "textures: file/solid/model-image, skeletons + clips: model, sounds: file)"
    );
    assetFactory().createMesh          = &createRecipeMesh;
    assetFactory().createTexture       = &createRecipeTexture;
    assetFactory().createSkeleton      = &createRecipeSkeleton;
    assetFactory().createAnimationClip = &createRecipeAnimationClip;
    assetFactory().createAudioClip     = &createRecipeAudioClip;
    assetFactory().decodeTexture       = &decodeTextureFile;
}

} // namespace Vkm::Engine
