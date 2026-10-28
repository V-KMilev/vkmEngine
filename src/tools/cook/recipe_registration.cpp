#define VKM_LOG_CATEGORY "ASSETS"

#include "cook/recipe_registration.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "io/asset/asset_factory.h"
#include "io/asset/asset_serializer.h"
#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "system/async/async_loader_system.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/texture_generators.h"
#include "import/audio_loaders.h"
#include "import/terrain_loaders.h"
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
    const std::string kind = source.value(AssetSourceKey::KIND, std::string{});
    LOG_ERROR("No %s import for recipe kind '%s'", what, kind.c_str());
    return {};
}

// The bases this thread's decimate imports are loading: a level whose chain of bases comes back to
// one of them names itself, and loading that base again would never return.
thread_local std::vector<std::string> t_basesInProgress;

MeshHandle createRecipeMesh(const nlohmann::json& source, ResourceManager& resources) {
    const std::string kind = source.value(AssetSourceKey::KIND, std::string{});

    // Synchronous, as a generator reads no file. Unwrapped here, as an imported mesh is: the
    // engine's generators run in the runtime, which carries no unwrapper.
    if (kind == AssetSourceKind::GENERATOR) {
        const MeshHandle handle = createGeneratedMesh(source, resources);
        if (handle) {
            MeshAsset mesh = resources.get(handle);
            unwrapLightmap(mesh, mesh.name());
            resources.swapValue(handle, mesh);
        }
        return handle;
    }

    // Async: parsing a model file is the slow path.
    if (kind == AssetSourceKind::MODEL) {
        return requestModelMeshAsync(
            source.value(AssetSourceKey::PATH, std::string{}),
            source.value(AssetSourceKey::MESH, -1),
            resources
        );
    }

    // A level the scene names without its base, as a terrain's scattered plants are, brings the base.
    if (kind == AssetSourceKind::DECIMATE) {
        const std::string baseName = source.value(AssetSourceKey::BASE, std::string{});
        const float ratio          = decimateRatioFromRecipe(source);
        if (!baseName.empty() && !resources.findByName<MeshAsset>(baseName)) {
            const auto inProgress = std::find(t_basesInProgress.begin(), t_basesInProgress.end(), baseName);
            if (inProgress != t_basesInProgress.end()) {
                LOG_ERROR(
                    "decimate: base mesh '%s' is a base of itself (LOD level dropped)",
                    baseName.c_str()
                );
                return {};
            }
            t_basesInProgress.push_back(baseName);
            AssetSerializer::loadNamed(AssetType::Mesh, baseName, resources);
            t_basesInProgress.pop_back();
        }
        const MeshHandle baseH = baseName.empty() ? MeshHandle{} : resources.findByName<MeshAsset>(baseName);
        if (!baseH) {
            LOG_ERROR("decimate: base mesh '%s' not loaded (LOD level dropped)", baseName.c_str());
            return {};
        }
        // Resident is not landed: the base decodes on the ThreadPool and no frame has finalised it
        // inside a scene load. Decimating a stub yields nothing. Its own landing is what counts here.
        awaitAsyncLoads(resources);
        if (resources.get(baseH).loading || resources.get(baseH).vertices.empty()) {
            LOG_ERROR("decimate: base mesh '%s' did not load (LOD level dropped)", baseName.c_str());
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
    const std::string kind = source.value(AssetSourceKey::KIND, std::string{});

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
    if (source.value(AssetSourceKey::KIND, std::string{}) == AssetSourceKind::MODEL) {
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
    if (source.value(AssetSourceKey::KIND, std::string{}) == AssetSourceKind::MODEL) {
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
    if (source.value(AssetSourceKey::KIND, std::string{}) == AssetSourceKind::FILE) {
        return loadAudioClip(source.value(AssetSourceKey::PATH, std::string{}), resources);
    }
    return refuseKind<AudioClipAsset>("sound", source);
}

TerrainHandle createRecipeTerrain(const nlohmann::json& source, ResourceManager& resources) {
    if (source.value(AssetSourceKey::KIND, std::string{}) == AssetSourceKind::FILE) {
        return loadTerrainFile(source.value(AssetSourceKey::PATH, std::string{}), resources);
    }
    return refuseKind<TerrainAsset>("terrain", source);
}

} // namespace

void registerRecipeAssetFactories() {
    LOG_INFO(
        "Registering recipe asset factories (meshes: generator/model/decimate, "
        "textures: file/solid/model-image, skeletons + clips: model, sounds and terrains: file)"
    );
    assetFactory().createMesh          = &createRecipeMesh;
    assetFactory().createTexture       = &createRecipeTexture;
    assetFactory().createSkeleton      = &createRecipeSkeleton;
    assetFactory().createAnimationClip = &createRecipeAnimationClip;
    assetFactory().createAudioClip     = &createRecipeAudioClip;
    assetFactory().createTerrain       = &createRecipeTerrain;
    assetFactory().decodeTexture       = &decodeTextureFile;
}

} // namespace Vkm::Engine
