#define VKM_LOG_CATEGORY "COOK"

#include "cook/asset_cooker.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "cook/cook_key.h"
#include "cook/mesh_processing.h"
#include "cook/texture_bake.h"
#include "core/fnv1a.h"
#include "core/reflect.h"
#include "io/asset/asset_cook.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "resource/resource_manager.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"
#include "platform/threading/thread_pool.h"
#include "system/async/async_loader_system.h"
#include "resource/asset_source_kind.h"

namespace Vkm::Engine::AssetCooker {

namespace {

// A decimated level is baked from its base mesh's output, not from a file.
bool isDecimated(const nlohmann::json& recipe) {
    return recipe.value("kind", std::string{}) == AssetSourceKind::DECIMATE;
}

// The asset a recipe is derived from: a decimated level's base, or a clip's rig from another
// file. An empty name depends on nothing.
struct Dependency {
    AssetType   type = AssetType::Count;
    std::string name;
};

Dependency dependencyOf(const nlohmann::json& recipe) {
    if (isDecimated(recipe)) return {AssetType::Mesh, recipe.value(AssetSourceKey::BASE, std::string{})};
    return {AssetType::Skeleton, recipe.value(AssetSourceKey::RIG, std::string{})};
}

/**
 * @brief What a record holds about where an asset came from.
 */
struct RecipeKey {
    uint64_t                 hash = 0;  ///< AssetRecord::recipeHash.
    std::vector<std::string> sources;   ///< AssetRecord::sources.
};

// The half of a key that reads the source art's bytes; see foldSourceContent.
RecipeKey keySources(const nlohmann::json& recipe) {
    RecipeKey key;
    key.sources = sourceFiles(recipe.value(AssetSourceKey::PATH, std::string{}));
    // A roughness map's bake reads its paired normal map's file too.
    for (std::string& file : sourceFiles(recipe.value(AssetSourceKey::ROUGHNESS_NORMAL, std::string{}))) {
        key.sources.push_back(std::move(file));
    }
    key.hash    = fnv1a64(recipe.dump());
    for (const std::string& file : key.sources) key.hash = foldSourceContent(file, key.hash);
    return key;
}

// @p key with its dependency's recorded key folded in, which moves when a base is cooked again.
RecipeKey withDependency(RecipeKey key, const nlohmann::json& recipe) {
    const Dependency dependency = dependencyOf(recipe);
    key.hash = foldDependency(dependency.type, dependency.name, key.hash);
    return key;
}

// A record's recipeHash, read off disk. The cooker's version is not in it: AssetCook::cacheKey
// adds that to the file's name.
RecipeKey keyRecipe(const nlohmann::json& recipe) {
    return withDependency(keySources(recipe), recipe);
}

/**
 * @brief The art-reading half of an asset's key, as a cook in this session read it.
 *
 * Kept while version and recipe are unchanged, so a save does not reread the art. A key read
 * from re-exported bytes would file what memory holds, made from the old ones, under them.
 */
struct RememberedKey {
    uint64_t  version = 0;  ///< Resource::version() when it was read.
    uint64_t  recipe  = 0;  ///< The recipe document's own hash then.
    RecipeKey key;
};

// By uid, on the cooking thread. Each cook keeps only what it asked about.
std::unordered_map<uint64_t, RememberedKey> g_lastCookKeys;
std::unordered_map<uint64_t, RememberedKey> g_thisCookKeys;

// keyRecipe for an asset a cook holds, reading its art only the first time.
RecipeKey sessionKey(const Resource& asset, const nlohmann::json& recipe) {
    const uint64_t recipeHash = fnv1a64(recipe.dump());
    const auto     last       = g_lastCookKeys.find(asset.uid());
    RememberedKey  remembered;
    if (last != g_lastCookKeys.end() && last->second.version == asset.version()
        && last->second.recipe == recipeHash) {
        remembered = last->second;
    } else {
        remembered = {asset.version(), recipeHash, keySources(recipe)};
    }
    g_thisCookKeys[asset.uid()] = remembered;
    return withDependency(remembered.key, recipe);
}

// The loader's stand-in source for a cooked-cache asset: cooking it would overwrite the library's
// version-controlled recipe with a self-reference.
bool isCookedPlaceholder(const nlohmann::json& source) {
    return source.value("kind", std::string{}) == AssetSourceKind::COOKED;
}

// An uncookable asset the library lacks is saved as a name the next load cannot resolve; say so
// while the user can act.
void warnUnlisted(AssetType type, const std::string& name) {
    if (AssetLibrary::get().find(type, name)) return;
    LOG_WARNING(
        "Cooker: %s '%s' has nothing to cook and no library entry; a scene referencing it will not load",
        Reflect::enumName(type),
        name.c_str()
    );
}

// Artifacts being baked on the ThreadPool. A bake releases its claim under this lock when it
// ends; nothing goes back through the main thread.
std::mutex                      g_bakingMutex;
std::set<std::filesystem::path> g_baking;

bool isBaking(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(g_bakingMutex);
    return g_baking.count(path) > 0;
}

// Bake @p path on the ThreadPool unless already under way; delete @p previous once it lands.
void bakeInBackground(
    const std::filesystem::path& path,
    const std::filesystem::path& previous,
    std::function<bool(const std::filesystem::path&)> write
) {
    {
        const std::lock_guard<std::mutex> lock(g_bakingMutex);
        if (!g_baking.insert(path).second) return;
    }
    ThreadPool::get().addTask([path, previous, write = std::move(write)]() {
        if (write(path)) {
            std::error_code ec;
            if (previous != path) std::filesystem::remove(previous, ec);
        } else {
            // Loads fall back to the recipe, and the next cook bakes it again.
            LOG_ERROR("Cooker: the background bake of '%s' failed", path.string().c_str());
        }
        const std::lock_guard<std::mutex> lock(g_bakingMutex);
        g_baking.erase(path);
    });
}

// A material's recipe IS its runtime form, so nothing is written where its cookedPath() points.
template<typename Asset>
constexpr bool WRITES_BINARY = !std::is_same_v<Asset, MaterialAsset>;

// Whether a kind's bake may run on the ThreadPool: the two that take seconds. The rest are copies.
template<typename Asset>
constexpr bool BAKES_IN_BACKGROUND = std::is_same_v<Asset, MeshAsset> || std::is_same_v<Asset, TextureAsset>;

// Whether a recorded asset can be skipped: the recipe and, for a kind with one, the binary must
// exist, not just the hash. The binary uses the loader's probe; one still baking counts as
// current, since a second bake would race it for the temporary.
bool isUpToDate(AssetType type, const std::string& name, uint64_t hash, bool writesBinary) {
    const AssetRecord* existing = AssetLibrary::get().find(type, name);
    if (!existing || existing->recipeHash != hash) return false;

    std::error_code ec;
    if (!std::filesystem::exists(AssetLibrary::recipePath(type, name), ec)) return false;
    if (!writesBinary) return true;
    const std::filesystem::path path = AssetLibrary::cookedPath(type, name, hash);
    return isBaking(path) || AssetCook::isCookedCurrent(type, path);
}

// What the asset holds once its import landed; empty means the source did not load.
bool hasContent(const MeshAsset& mesh)          { return !mesh.loading && !mesh.vertices.empty(); }
bool hasContent(const TextureAsset& texture)    { return !texture.loading && !texture.pixelData.empty(); }
bool hasContent(const SkeletonAsset& skeleton)  { return !skeleton.bones.empty(); }
bool hasContent(const AnimationClipAsset& clip) { return !clip.bones.empty(); }
bool hasContent(const AudioClipAsset& clip)     { return clip.sampleCount() > 0; }

// Write each kind's artifact and say what was baked. A mesh is reordered on a copy: the asset is
// the graph's, and the cook only reads it.
bool writeCooked(const std::filesystem::path& path, const MeshAsset& mesh) {
    MeshAsset baked = mesh;
    optimizeMeshForGpu(baked);
    if (!AssetCook::writeMesh(path, baked)) return false;
    LOG_INFO(
        "Cooked mesh '%s' (%zu verts, %zu indices)",
        baked.name().c_str(),
        baked.vertices.size(),
        baked.indices.size()
    );
    return true;
}

// A roughness map's paired normal map, decoded from its source file rather than taken from the
// graph, which may hold it cooked; false for none, no host decode, or one that will not decode
// (the roughness then bakes as it is).
bool pairedNormal(const TextureAsset& source, TextureAsset& normal) {
    const std::string ref = source.sourceJson().value(AssetSourceKey::ROUGHNESS_NORMAL, std::string{});
    const TextureDecode decode = assetFactory().decodeTexture;
    if (ref.empty() || !decode) return false;
    if (!decode(ref, TextureUsage::Normal, normal)) {
        LOG_WARNING("Texture '%s': its normal map '%s' did not decode", source.name().c_str(), ref.c_str());
        return false;
    }
    return true;
}

// The asset keeps its decoded pixels; the file gets the mip chain and blocks.
bool writeCooked(const std::filesystem::path& path, const TextureAsset& source) {
    TextureAsset normal;
    const bool   paired = pairedNormal(source, normal);
    TextureAsset baked;
    if (!bakeTexture(source, baked, paired ? &normal : nullptr) || !AssetCook::writeTexture(path, baked)) {
        return false;
    }
    LOG_INFO(
        "Cooked texture '%s' (%ux%u, %u level(s)%s)",
        source.name().c_str(),
        source.params.width,
        source.params.height,
        baked.params.mipLevels,
        isCompressedFormat(baked.params.internalFormat) ? ", block-compressed" : ""
    );
    return true;
}

bool writeCooked(const std::filesystem::path& path, const SkeletonAsset& skeleton) {
    if (!AssetCook::writeSkeleton(path, skeleton)) return false;
    LOG_INFO("Cooked skeleton '%s' (%zu bones)", skeleton.name().c_str(), skeleton.bones.size());
    return true;
}

bool writeCooked(const std::filesystem::path& path, const AnimationClipAsset& clip) {
    if (!AssetCook::writeAnimationClip(path, clip)) return false;
    LOG_INFO(
        "Cooked clip '%s' (%.2fs, %zu bones)",
        clip.name().c_str(),
        static_cast<double>(clip.duration),
        clip.bones.size()
    );
    return true;
}

bool writeCooked(const std::filesystem::path& path, const AudioClipAsset& clip) {
    if (!AssetCook::writeAudioClip(path, clip)) return false;
    LOG_INFO(
        "Cooked sound '%s' (%.2fs, %u channel(s), %u Hz)",
        clip.name().c_str(),
        static_cast<double>(clip.duration()),
        clip.channels,
        clip.sampleRate
    );
    return true;
}

/**
 * @brief Record one asset in the library and write its artifact, unless both are current.
 *
 * The previous record's artifact is deleted once replaced, or cooked/ grows a file per re-cook. A
 * background bake works on a copy taken only once there is something to bake; loads fall back to
 * the recipe until it lands.
 *
 * @tparam Asset The asset type being cooked.
 * @param asset  The asset; its name is its key in the library.
 * @param recipe What the artifact is derived from, and what the key is hashed from.
 * @param bake   Whether a mesh's or texture's artifact is written before this returns or after.
 * @return False when the recipe, the record or the artifact could not be written.
 */
template<typename Asset>
bool cookThrough(const Asset& asset, const nlohmann::json& recipe, Bake bake) {
    constexpr AssetType TYPE = ASSET_TYPE<Asset>;
    const std::string& name = asset.name();
    RecipeKey          key  = sessionKey(asset, recipe);
    const uint64_t     hash = key.hash;

    if (isUpToDate(TYPE, name, hash, WRITES_BINARY<Asset>)) return true;

    if (!AssetLibrary::writeRecipe(TYPE, name, recipe)) return false;
    const std::filesystem::path cookedPath = AssetLibrary::cookedPath(TYPE, name, hash);
    const bool now = !BAKES_IN_BACKGROUND<Asset> || bake == Bake::Now;
    if constexpr (WRITES_BINARY<Asset>) {
        if (now && !writeCooked(cookedPath, asset)) return false;
    }

    const AssetRecord* previous = AssetLibrary::get().find(TYPE, name);
    const std::filesystem::path previousPath = previous
        ? AssetLibrary::cookedPath(TYPE, name, previous->recipeHash)
        : cookedPath;
    AssetLibrary::get().upsert({TYPE, name, hash, std::move(key.sources)});

    if constexpr (!WRITES_BINARY<Asset>) {
        LOG_INFO("Cooked material '%s'", name.c_str());
    } else if (!now) {
        bakeInBackground(cookedPath, previousPath, [baked = asset](const std::filesystem::path& path) {
            return writeCooked(path, baked);
        });
    } else if (previousPath != cookedPath) {
        std::error_code ec;
        std::filesystem::remove(previousPath, ec);
    }
    return true;
}

/**
 * @brief Cook one asset, or say why there is nothing to cook.
 *
 * A material's recipe is computed from what it holds. Any other kind's is the source it was
 * imported from; one with none, or served from the cooked cache, has nothing to re-cook, and one
 * whose import produced nothing is an error, since the manifest would promise a file nothing made.
 *
 * @tparam Asset     The asset type being cooked.
 * @param asset      The asset; unnamed ones are skipped.
 * @param resources  Resolves a material's texture handles to names.
 * @param bake       Passed to cookThrough.
 * @return False only on a real failure; a skip returns true.
 */
template<typename Asset>
bool cookAsset(const Asset& asset, const ResourceManager& resources, Bake bake) {
    if constexpr (!WRITES_BINARY<Asset>) {
        return cookThrough(asset, AssetSerializer::materialToInline(asset, resources), bake);
    } else {
        constexpr AssetType TYPE = ASSET_TYPE<Asset>;
        if (!asset.hasSource() || isCookedPlaceholder(asset.sourceJson())) {
            warnUnlisted(TYPE, asset.name());
            return true;
        }
        if (!hasContent(asset)) {
            LOG_ERROR(
                "Cooker: %s '%s' has a recipe but nothing to bake; its source did not load",
                Reflect::enumName(TYPE),
                asset.name().c_str()
            );
            return false;
        }
        return cookThrough(asset, asset.sourceJson(), bake);
    }
}

// Every asset of one kind and one side of the derived split; how many failed.
template<typename Asset>
size_t cookEach(const ResourceManager& resources, bool derived, Bake bake) {
    size_t failed = 0;
    resources.forEachOfType<Asset>([&](Handle<Asset>, const Asset& asset) {
        if (asset.isHidden() || asset.name().empty()) return;
        if ((asset.hasSource() && isDecimated(asset.sourceJson())) != derived) return;
        if (!cookAsset(asset, resources, bake)) ++failed;
    });
    return failed;
}

size_t cookPass(AssetType type, bool derived, const ResourceManager& resources, Bake bake) {
    switch (type) {
#define VKM_COOK_PASS(tag, Asset, kindName, dir) \
        case AssetType::tag: return cookEach<Asset>(resources, derived, bake);
        VKM_ASSET_KINDS(VKM_COOK_PASS)
#undef VKM_COOK_PASS
        case AssetType::Count: break;
    }
    return 0;
}

// Calls @p pass for every kind in ASSET_DEPENDENCY_ORDER, a derived asset after every base: a
// decimated level is keyed by its base's recorded key, which a later re-cook of the base would move.
template<typename Pass>
void inDependencyOrder(Pass&& pass) {
    for (const AssetType type : ASSET_DEPENDENCY_ORDER) {
        for (const bool derived : {false, true}) pass(type, derived);
    }
}

} // namespace

bool cookAllAssets(ResourceManager& resources, Bake bake) {
    LOG_INFO("Cooking assets into the library...");
    g_lastCookKeys = std::move(g_thisCookKeys);
    g_thisCookKeys.clear();

    size_t failed = awaitAsyncLoads(resources) ? 0 : 1;
    inDependencyOrder([&](AssetType type, bool derived) {
        failed += cookPass(type, derived, resources, bake);
    });

    // A failed asset writes no record this cook; whatever it had recorded before stands.
    if (failed > 0) {
        LOG_ERROR(
            "Cooker: %zu asset(s) failed to cook; their manifest records are left as they were",
            failed
        );
    }

    bool saved = AssetLibrary::get().save();
    if (!saved) {
        LOG_ERROR("Cooker: failed to save the asset library manifest");
    }
    return failed == 0 && saved;
}

namespace {

// One asset the sweep re-imports.
struct StaleAsset {
    AssetType      type = AssetType::Count;
    std::string    name;
    nlohmann::json recipe;
};

// Run the recipe through the host's import, filed under the manifest's (the scene's) name.
template<typename Asset>
bool importAs(const StaleAsset& stale, ResourceManager& resources) {
    const RecipeImport<Asset> import = recipeImport<Asset>();
    if (!import) return false;
    const Handle<Asset> handle = import(stale.recipe, resources);
    if (!handle) return false;
    resources.rename(handle, stale.name);
    return true;
}

bool importRecipe(const StaleAsset& stale, ResourceManager& resources) {
    switch (stale.type) {
        case AssetType::Mesh:          return importAs<MeshAsset>(stale, resources);
        case AssetType::Texture:       return importAs<TextureAsset>(stale, resources);
        case AssetType::Skeleton:      return importAs<SkeletonAsset>(stale, resources);
        case AssetType::AnimationClip: return importAs<AnimationClipAsset>(stale, resources);
        case AssetType::AudioClip:     return importAs<AudioClipAsset>(stale, resources);
        case AssetType::Material:      // its recipe is its runtime form; never stale
        case AssetType::Count:         break;
    }
    return false;
}

} // namespace

bool cookStaleAssets() {
    const AssetLibrary& library = AssetLibrary::get();

    std::vector<StaleAsset> stale;
    std::vector<StaleAsset> dependencies;
    std::set<std::pair<AssetType, std::string>> staleNames;
    std::set<std::pair<AssetType, std::string>> dependencyNames;

    // In dependency order: a derived asset is stale when its base is, as its key folds the base's.
    // A material has no binary to re-bake.
    const auto sweep = [&](AssetType type, bool derived) {
        if (type == AssetType::Material) return;
        for (const std::string& name : library.namesOf(type)) {
            nlohmann::json recipe;
            if (!AssetLibrary::readRecipe(type, name, recipe)) {
                LOG_WARNING(
                    "Cooker: %s '%s' is in the manifest with no recipe to check it against",
                    Reflect::enumName(type),
                    name.c_str()
                );
                continue;
            }
            if (isDecimated(recipe) != derived) continue;

            const uint64_t recorded   = library.find(type, name)->recipeHash;
            const Dependency depends  = dependencyOf(recipe);
            const bool baseMoved      = staleNames.count({depends.type, depends.name}) > 0;
            const bool recipeMoved    = keyRecipe(recipe).hash != recorded;
            const bool artifactMissed = !AssetCook::isCookedCurrent(
                type,
                AssetLibrary::cookedPath(type, name, recorded)
            );
            if (!baseMoved && !recipeMoved && !artifactMissed) continue;

            // Its base must be in the graph for it to import; the cook finds the base current.
            if (!depends.name.empty() && !baseMoved
                && dependencyNames.insert({depends.type, depends.name}).second) {
                nlohmann::json base;
                if (AssetLibrary::readRecipe(depends.type, depends.name, base)) {
                    dependencies.push_back({depends.type, depends.name, std::move(base)});
                }
            }
            staleNames.insert({type, name});
            stale.push_back({type, name, std::move(recipe)});
        }
    };
    inDependencyOrder(sweep);

    if (stale.empty()) return true;
    LOG_INFO("Cooker: %zu asset(s) changed since they were baked; re-importing them", stale.size());

    ResourceManager resources;
    size_t failed = 0;
    for (const std::vector<StaleAsset>* list : {&dependencies, &stale}) {
        for (const StaleAsset& import : *list) {
            if (importRecipe(import, resources)) continue;
            LOG_ERROR(
                "Cooker: %s '%s' did not import from its recipe",
                Reflect::enumName(import.type),
                import.name.c_str()
            );
            ++failed;
        }
    }
    return cookAllAssets(resources) && failed == 0;
}

} // namespace Vkm::Engine::AssetCooker
