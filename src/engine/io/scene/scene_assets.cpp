#define VKM_LOG_CATEGORY "IO"

#include "io/scene/scene_assets.h"

#include <array>
#include <exception>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "vkm_log/logger.h"

#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "debug/error/engine_error_log.h"
#include "ecs/scene.h"
#include "resource/asset/terrain_asset.h"
#include "resource/resource_manager.h"
#include "io/asset/recipe/asset_factory.h"
#include "io/asset/recipe/material_recipe.h"
#include "io/asset/asset_library.h"
#include "io/asset/cooked/cooked_loader.h"
#include "io/asset/terrain_file.h"
#include "io/json/reflect_json.h"
#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"

namespace Vkm::Engine::SceneAssets {

namespace {

/**
 * @brief Emit one name-only asset reference into @p target.
 *
 * Skipped here, so every emitter keeps the rule: an unnamed asset has no key, and a hidden one is absent
 * from the cooked manifest, so its name would name nothing on load.
 *
 * @param target The section array the reference is appended to.
 * @param asset  The asset to name.
 */
void emitDescriptor(nlohmann::json& target, const Resource& asset) {
    if (asset.isHidden()) return;
    if (asset.name().empty()) {
        LOG_WARNING("Asset has no name; skipping in save");
        return;
    }
    target.push_back({{"name", asset.name()}});
}

/**
 * @brief Emit a name-only reference that was authored as a name, not held as a handle.
 *
 * With no handle to dedup by, and a name a component may already have listed, the section itself is
 * checked - a scan over lists a scene keeps short.
 *
 * @param target The section array the reference is appended to.
 * @param name   The authored asset name; skipped when the section already has it.
 */
void emitNamedRef(nlohmann::json& target, const std::string& name) {
    for (const auto& entry : target) {
        if (entry.value("name", std::string{}) == name) return;
    }
    target.push_back({{"name", name}});
}

} // namespace

nlohmann::json saveAssetsForEntities(
    const Scene& scene,
    const std::vector<EntityId>& entities,
    const ResourceManager& resources
) {
    // One section per kind, indexed by AssetType, with the handles already in it.
    struct Section {
        nlohmann::json               entries = nlohmann::json::array();
        std::unordered_set<uint32_t> seen;
    };
    std::array<Section, static_cast<size_t>(AssetType::Count)> sections;

    // The asset the first time a handle is seen, and null every time after.
    const auto emit = [&](const auto& handle) {
        using Asset = typename std::decay_t<decltype(handle)>::resource_t;
        Section& section     = sections[static_cast<size_t>(ASSET_TYPE<Asset>)];
        const Asset* already = nullptr;
        if (!handle || !section.seen.insert(handle.id()).second) return already;
        const Asset& asset = resources.get(handle);
        emitDescriptor(section.entries, asset);
        return &asset;
    };

    EntityAssetRefs collected;
    for (EntityId id : entities) collectAssetRefs(scene, id, resources, collected);
    const ComponentSerializer::AssetRefs& refs = collected.handles;

    // A material's textures too, so the loader recreates them first - unless hidden: a thumbnail material
    // must not drag its textures into the user's scene.
    const auto emitTextures = [&](const MaterialAsset* material) {
        if (!material || material->isHidden()) return;
        for (const auto& f : MATERIAL_TEXTURE_FIELDS) emit(material->*f.member);
    };

    // A terrain's layers and plants too, which it names: without them its next load draws nothing
    // where they were painted. A plant's mesh brings its levels, which only it names
    // (TerrainPlantDraw::levels).
    const auto emitMaterial = [&](MaterialHandle material) { emitTextures(emit(material)); };
    const auto emitPlantMesh = [&](MeshHandle mesh) {
        const MeshAsset* asset = emit(mesh);
        if (!asset) return;
        for (uint32_t level = 1; level <= PLANT_LEVELS; ++level) {
            emit(resources.findByName<MeshAsset>(LOD::levelName(asset->name(), level)));
        }
    };
    const auto emitTerrain = [&](const TerrainAsset* terrain) {
        if (terrain) terrain->forEachReference(emitMaterial, emitPlantMesh);
    };

    // Every handle, then what its asset names: a material its textures, a terrain its layers and plants.
    const auto emitWith = [&](const auto* asset) {
        using Asset = std::decay_t<decltype(*asset)>;
        if constexpr (std::is_same_v<Asset, MaterialAsset>) emitTextures(asset);
        if constexpr (std::is_same_v<Asset, TerrainAsset>) emitTerrain(asset);
    };
#define VKM_ASSET_EMIT_REFS(tag, Asset, name, dir, cooked) \
    for (const Handle<Asset>& handle : refs.dir) emitWith(emit(handle));
    VKM_ASSET_KINDS(VKM_ASSET_EMIT_REFS)
#undef VKM_ASSET_EMIT_REFS

    // Flat, by name. A material named this way that is loaded lists its textures, as one held by handle does.
    for (const auto& [type, name] : collected.names) {
        if (type == AssetType::Count) continue;
        emitNamedRef(sections[static_cast<size_t>(type)].entries, name);
        if (type == AssetType::Material) {
            emitTextures(resources.tryGet(resources.findByName<MaterialAsset>(name)));
        }
        if (type == AssetType::Terrain) {
            emitTerrain(resources.tryGet(resources.findByName<TerrainAsset>(name)));
        }
    }

    // A section is keyed by its kind's library directory name, one list for both.
    nlohmann::json out;
#define VKM_ASSET_SECTION_OUT(tag, type, name, dir, cooked) \
    out[#dir] = std::move(sections[static_cast<size_t>(AssetType::tag)].entries);
    VKM_ASSET_KINDS(VKM_ASSET_SECTION_OUT)
#undef VKM_ASSET_SECTION_OUT
    return out;
}


namespace {

/**
 * @brief Every live asset of one type, as the name-only descriptors a section holds.
 *
 * @tparam Asset Asset type to enumerate.
 * @param resources The graph to walk.
 * @return An array of descriptors, empty when the graph holds none.
 */
template<typename Asset>
nlohmann::json everyAssetOfType(const ResourceManager& resources) {
    nlohmann::json section = nlohmann::json::array();
    resources.forEachOfType<Asset>([&section](Handle<Asset>, const Asset& asset) {
        emitDescriptor(section, asset);
    });
    return section;
}

/**
 * @brief Remove every asset of one kind whose name the section does not carry.
 *
 * Handles are collected first: removal swap-and-pops the storage being walked. An absent section says
 * nothing about its kind, so nothing of that kind is dropped.
 *
 * @tparam Asset Resource type this section holds.
 * @param assetsJson The whole document.
 * @param key Section name within it.
 * @param resources Graph to prune.
 * @return How many were removed.
 */
template<typename Asset>
size_t dropSectionExtras(const nlohmann::json& assetsJson, const char* key, ResourceManager& resources) {
    const auto section = assetsJson.find(key);
    if (section == assetsJson.end() || !section->is_array()) return 0;

    std::unordered_set<std::string> keep;
    for (const nlohmann::json& entry : *section) {
        if (entry.is_object() && entry.contains("name") && entry["name"].is_string()) {
            keep.insert(entry["name"].get<std::string>());
        }
    }

    std::vector<Handle<Asset>> doomed;
    resources.template forEachOfType<Asset>([&](Handle<Asset> handle, const Asset& asset) {
        if (asset.isHidden() || keep.count(asset.name()) > 0) return;
        doomed.push_back(handle);
    });
    for (const Handle<Asset>& handle : doomed) resources.remove(handle);
    return doomed.size();
}

} // namespace

std::vector<std::pair<std::string, std::string>> namesIn(const nlohmann::json& block) {
    std::vector<std::pair<std::string, std::string>> names;
    if (!block.is_object()) return names;
    for (auto section = block.begin(); section != block.end(); ++section) {
        if (!section.value().is_array()) continue;
        for (const nlohmann::json& entry : section.value()) {
            const std::string name = entry.value("name", std::string{});
            if (!name.empty()) names.emplace_back(section.key(), name);
        }
    }
    return names;
}

nlohmann::json saveAllAssets(const ResourceManager& resources) {
    nlohmann::json out;
    // No dependency order to keep: these are names, and loadAssets reads sections in the order it needs.
#define VKM_ASSET_SECTION_ALL(tag, type, name, dir, cooked) out[#dir] = everyAssetOfType<type>(resources);
    VKM_ASSET_KINDS(VKM_ASSET_SECTION_ALL)
#undef VKM_ASSET_SECTION_ALL
    return out;
}

size_t dropAssetsNotIn(const nlohmann::json& assetsJson, ResourceManager& resources) {
    if (!assetsJson.is_object()) return 0;

    // The kinds saveAllAssets writes, from the same list, so what is pruned is what could be recorded.
    size_t dropped = 0;
#define VKM_ASSET_SECTION_DROP(tag, type, name, dir, cooked) \
    dropped += dropSectionExtras<type>(assetsJson, #dir, resources);
    VKM_ASSET_KINDS(VKM_ASSET_SECTION_DROP)
#undef VKM_ASSET_SECTION_DROP

    if (dropped > 0) LOG_INFO("Dropped %zu asset(s) the session created", dropped);
    return dropped;
}

nlohmann::json saveAssetsForScene(const Scene& scene, const ResourceManager& resources) {
    std::vector<EntityId> entities;
    entities.reserve(scene.entityCount());
    scene.forEachEntity([&](EntityId id) { entities.push_back(id); });
    return saveAssetsForEntities(scene, entities, resources);
}

namespace {

/**
 * @brief Build one asset from the library: its cooked file when this build can read one, else its recipe.
 *
 * The recipe is the source of truth: after a cooker version bump it is read and the next cook re-bakes. A
 * material has no cooked file; its recipe is its runtime form. Nor has a terrain: its .terrain file, which
 * its name names, is read as it is.
 *
 * @tparam Asset    Resource type to build.
 * @param name      The asset's library name.
 * @param resources The asset graph to build into.
 * @return The asset, or an invalid handle, having logged, when neither source serves it.
 */
template<typename Asset>
Handle<Asset> loadFromLibrary(const std::string& name, ResourceManager& resources) {
    constexpr AssetType TYPE = ASSET_TYPE<Asset>;
    nlohmann::json recipe;
    if constexpr (std::is_same_v<Asset, MaterialAsset>) {
        if (!AssetLibrary::readRecipe(TYPE, name, recipe)) return {};
        return resources.add(materialFromRecipe(recipe, name, resources));
    } else if constexpr (std::is_same_v<Asset, TerrainAsset>) {
        return TerrainFile::load(name, resources);
    } else {
        if (const Handle<Asset> cooked = loadCooked<Asset>(name, resources)) return cooked;

        const RecipeImport<Asset> import = recipeImport<Asset>();
        const char* what = Reflect::enumName(TYPE);
        if (!import) {
            LOG_ERROR(
                "%s '%s': no cooked file this build can read, and this host imports no recipes",
                what,
                name.c_str()
            );
            return {};
        }
        LOG_INFO("%s '%s': no cooked file this build can read; importing its recipe", what, name.c_str());
        if (!AssetLibrary::readRecipe(TYPE, name, recipe)) return {};
        return import(recipe, resources);
    }
}

/**
 * @brief What loadOne did about a name.
 */
enum class Loaded { Created, Held, Failed };

/**
 * @brief Recreate one asset by its library name.
 *
 * Built by loadFromLibrary and renamed to @p name. Under LoadMode::Reload a held material is rebuilt
 * into its own slot (moved onto the live one, committed for re-upload, the shell dropped), so every
 * handle keeps naming it.
 *
 * @tparam Asset    Resource type to build.
 * @param name      Its name in the library; empty is warned of and fails.
 * @param resources The asset graph to build into.
 * @param mode      What to do about a name the graph already holds.
 * @return Whether it was created, already held (or rebuilt in place), or failed, having logged.
 */
template<typename Asset>
Loaded loadOne(const std::string& name, ResourceManager& resources, LoadMode mode);

/**
 * @brief Load the maps material @p name's recipe names that the graph lacks.
 *
 * A document lists what it uses, but a map named only in the recipe would otherwise resolve to
 * nothing, and the next save would drop it.
 *
 * @param name      The material.
 * @param resources The asset graph to build into.
 */
void loadMaterialMaps(const std::string& name, ResourceManager& resources) {
    nlohmann::json recipe;
    if (!AssetLibrary::readRecipe(AssetType::Material, name, recipe)) return;
    const auto refs = recipe.find("textures");
    if (refs == recipe.end() || !refs->is_object()) return;
    for (const auto& [slot, ref] : refs->items()) {
        if (ref.is_string() && !ref.get<std::string>().empty()) {
            loadOne<TextureAsset>(ref.get<std::string>(), resources, LoadMode::Create);
        }
    }
}

template<typename Asset>
Loaded loadOne(const std::string& name, ResourceManager& resources, LoadMode mode) {
    constexpr AssetType TYPE = ASSET_TYPE<Asset>;
    const char* what = Reflect::enumName(TYPE);
    if (name.empty()) {
        LOG_WARNING("%s entry missing 'name' - skipping", what);
        return Loaded::Failed;
    }
    // Reload rebuilds a material alone; LoadMode::Reload says why.
    const Handle<Asset> live = resources.findByName<Asset>(name);
    const bool rebuild = mode == LoadMode::Reload && std::is_same_v<Asset, MaterialAsset>;
    if (live && !rebuild) return Loaded::Held;

    // A hand-edited recipe can hold a wrong-typed value that throws: that costs this asset, named, not
    // the scene; its references stay unresolved, as a missing one's do.
    const size_t held = resources.countOfType<Asset>();
    Handle<Asset> h;
    try {
        if constexpr (std::is_same_v<Asset, MaterialAsset>) loadMaterialMaps(name, resources);
        h = loadFromLibrary<Asset>(name, resources);
    } catch (const std::exception& e) {
        reportError(
            "Asset",
            std::string(what) + " '" + name + "'",
            "its recipe " + AssetLibrary::recipePath(TYPE, name).string()
                + " does not read (" + e.what() + "); left unresolved"
        );
        return Loaded::Failed;
    }
    if (!h) {
        LOG_WARNING("%s '%s' could not be recreated - skipping", what, name.c_str());
        return Loaded::Failed;
    }
    // An importer dedupes by its own identity (a texture by its path), so it can answer with an asset
    // held under another name; renaming or moving that takes it from everything resolving it.
    if (h != live && resources.countOfType<Asset>() == held
        && resources.get(h).name() != name) {
        LOG_WARNING(
            "%s '%s': its recipe resolves to '%s', which is an asset of its own; "
                "left unresolved (two library entries import the same source)",
            what,
            name.c_str(),
            resources.get(h).name().c_str()
        );
        return Loaded::Failed;
    }
    if (!live) {
        resources.rename(h, name);
        return Loaded::Created;
    }
    // A load answering with the live asset rebuilt nothing; removing it would delete the live one.
    if (h == live) return Loaded::Held;

    // The rebuilt contents go into the live slot so its handle keeps naming it; the shell comes back
    // holding the old contents and is removed like any other asset.
    resources.swapValue(live, resources.edit(h));
    resources.remove(h);
    return Loaded::Held;
}

/**
 * @brief Recreate one asset kind's section, each entry by loadOne.
 *
 * @tparam Asset     Resource type the section holds.
 * @param assetsJson The whole assets block.
 * @param sectionKey The section's key, its kind's library directory name.
 * @param resources  The asset graph to build into.
 * @param mode       What to do about a name the graph already holds.
 * @return {created, already present}.
 */
template<typename Asset>
std::pair<size_t, size_t> loadAssetSection(
    const nlohmann::json& assetsJson,
    const char* sectionKey,
    ResourceManager& resources,
    LoadMode mode
) {
    size_t created = 0, skipped = 0;
    auto it = assetsJson.find(sectionKey);
    if (it == assetsJson.end() || !it->is_array()) return {created, skipped};

    for (const auto& entry : *it) {
        // An entry that is no object, or whose name is no string, costs only itself, as one with no
        // name does; json::value would throw and refuse the whole document.
        const auto named = entry.find("name");
        const std::string name = named != entry.end() && named->is_string() ? named->get<std::string>() : "";
        const Loaded loaded = loadOne<Asset>(name, resources, mode);
        if (loaded == Loaded::Created) ++created;
        if (loaded == Loaded::Held) ++skipped;
    }
    return {created, skipped};
}

// One kind's section: its key and type come from its row of VKM_ASSET_KINDS.
std::pair<size_t, size_t> loadSection(
    AssetType type,
    const nlohmann::json& assetsJson,
    ResourceManager& resources,
    LoadMode mode
) {
    switch (type) {
#define VKM_LOAD_SECTION(tag, Asset, kindName, dir, cooked) \
        case AssetType::tag:                                 \
            return loadAssetSection<Asset>(assetsJson, #dir, resources, mode);
        VKM_ASSET_KINDS(VKM_LOAD_SECTION)
#undef VKM_LOAD_SECTION
        case AssetType::Count: break;
    }
    return {0, 0};
}

} // namespace

bool loadNamed(AssetType type, const std::string& name, ResourceManager& resources) {
    switch (type) {
#define VKM_LOAD_NAMED(tag, Asset, kindName, dir, cooked) \
        case AssetType::tag:                               \
            return loadOne<Asset>(name, resources, LoadMode::Create) != Loaded::Failed;
        VKM_ASSET_KINDS(VKM_LOAD_NAMED)
#undef VKM_LOAD_NAMED
        case AssetType::Count: break;
    }
    return false;
}

bool loadAssets(const nlohmann::json& assetsJson, ResourceManager& resources, LoadMode mode) {
    if (!assetsJson.is_object()) {
        LOG_WARNING("Assets block is not an object - skipping");
        return false;
    }

    size_t created = 0;
    size_t skipped = 0;
    std::string tally;
    for (const AssetType type : ASSET_DEPENDENCY_ORDER) {
        const auto [made, kept] = loadSection(type, assetsJson, resources, mode);
        created += made;
        skipped += kept;
        if (made > 0) {
            tally += (tally.empty() ? "" : ", ") + std::to_string(made) + " " + Reflect::enumName(type);
        }
    }

    // Silent when nothing is new: a prefab's assets block is loaded again every time the prefab is read.
    if (created > 0) {
        LOG_INFO("Assets created: %s; %zu already loaded", tally.c_str(), skipped);
    }
    return true;
}

} // namespace Vkm::Engine::SceneAssets
