#define VKM_LOG_CATEGORY "IO"

#include "io/scene/scene_serializer.h"

#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/entity.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/component_serializer.h"
#include "io/json_file.h"
#include "io/json_vec.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "ecs/component/prefab_instance.h"

namespace Engine::SceneSerializer {

namespace {

using nlohmann::json;
namespace CS = ComponentSerializer;

// Assets are name-only references resolved through the cooked asset library.
constexpr int FILE_FORMAT_VERSION = 2;

/**
 * @brief Per-component serialization, as a flat explicit list.
 *
 * saveComponents / loadComponents handle one component type per line, in the
 * same order. Adding a component is a localised edit: add a line to each of
 * the two functions plus an entry to COMPONENT_KEYS below.
 *
 * Special cases:
 *  - Mesh references assets by handle, so save/load take a ResourceManager
 *    (resolution happens against the staging RM on load).
 *  - Hierarchy is save-only here: load captures the parent index in pass 1
 *    and wires it up in pass 2 via HierarchyOperations::setParent, because
 *    the parent entity may not exist yet when the child is first seen.
 */

// Every JSON key written by saveComponents, for unknown-key detection on load.
// Order is incidental here (membership test only); keep it in sync with the
// save/load lists below.
constexpr std::array<const char*, 20> COMPONENT_KEYS = {
    "Name", "Transform", "Camera", "Light", "Rigidbody", "Collider",
    "Mesh", "LOD", "Decal", "ParticleEmitter", "IrradianceVolume", "ReflectionProbe",
    "Animation", "Script", "Hierarchy",
    "UICanvas", "UIElement", "UIImage", "UIText", "UIButton",
};

} // namespace

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    if (s.has<Name>(id))            c["Name"]         = CS::save(s.get<Name>(id));
    if (s.has<Transform>(id))       c["Transform"]    = CS::save(s.get<Transform>(id));
    if (s.has<Camera>(id))          c["Camera"]       = CS::save(s.get<Camera>(id));
    if (s.has<Light>(id))           c["Light"]        = CS::save(s.get<Light>(id));
    if (s.has<Rigidbody>(id))       c["Rigidbody"]    = CS::save(s.get<Rigidbody>(id));
    if (s.has<Collider>(id))        c["Collider"]     = CS::save(s.get<Collider>(id));
    if (s.has<Mesh>(id))            c["Mesh"]         = CS::save(s.get<Mesh>(id), r);
    if (s.has<LOD>(id))             c["LOD"]          = CS::save(s.get<LOD>(id), r);
    if (s.has<Decal>(id))           c["Decal"]        = CS::save(s.get<Decal>(id), r);
    if (s.has<ParticleEmitter>(id)) c["ParticleEmitter"] = CS::save(s.get<ParticleEmitter>(id));
    if (s.has<IrradianceVolume>(id)) c["IrradianceVolume"] = CS::save(s.get<IrradianceVolume>(id));
    if (s.has<ReflectionProbe>(id))  c["ReflectionProbe"]  = CS::save(s.get<ReflectionProbe>(id));
    if (s.has<Animation>(id))       c["Animation"]    = CS::save(s.get<Animation>(id));
    if (s.has<ScriptComponent>(id)) c["Script"]       = CS::save(s.get<ScriptComponent>(id));
    if (s.has<Hierarchy>(id))       c["Hierarchy"]    = CS::save(s.get<Hierarchy>(id));
    if (s.has<UICanvas>(id))        c["UICanvas"]     = CS::save(s.get<UICanvas>(id));
    if (s.has<UIElement>(id))       c["UIElement"]    = CS::save(s.get<UIElement>(id));
    if (s.has<UIImage>(id))         c["UIImage"]      = CS::save(s.get<UIImage>(id));
    if (s.has<UIText>(id))          c["UIText"]       = CS::save(s.get<UIText>(id));
    if (s.has<UIButton>(id))        c["UIButton"]     = CS::save(s.get<UIButton>(id));
}

// Hierarchy is intentionally absent: its parent link is captured by the
// caller for the pass-2 wire-up, not loaded here.
void loadComponents(const json& src, Scene& s, Entity e, const ResourceManager& r) {
    if (src.contains("Name"))         { Name c;            CS::load(src["Name"], c);            s.add(e, std::move(c)); }
    if (src.contains("Transform"))    { Transform c;       CS::load(src["Transform"], c);       s.add(e, std::move(c)); }
    if (src.contains("Camera"))       { Camera c;          CS::load(src["Camera"], c);          s.add(e, std::move(c)); }
    if (src.contains("Light"))        { Light c;           CS::load(src["Light"], c);           s.add(e, std::move(c)); }
    if (src.contains("Rigidbody"))    { Rigidbody c;       CS::load(src["Rigidbody"], c);       s.add(e, std::move(c)); }
    if (src.contains("Collider"))     { Collider c;        CS::load(src["Collider"], c);        s.add(e, std::move(c)); }
    if (src.contains("Mesh"))         { Mesh c;            CS::load(src["Mesh"], c, r);         s.add(e, std::move(c)); }
    if (src.contains("LOD"))          { LOD c;             CS::load(src["LOD"], c, r);          s.add(e, std::move(c)); }
    if (src.contains("Decal"))        { Decal c;           CS::load(src["Decal"], c, r);        s.add(e, std::move(c)); }
    if (src.contains("ParticleEmitter")) { ParticleEmitter c; CS::load(src["ParticleEmitter"], c); s.add(e, std::move(c)); }
    if (src.contains("IrradianceVolume")) { IrradianceVolume c; CS::load(src["IrradianceVolume"], c); s.add(e, std::move(c)); }
    if (src.contains("ReflectionProbe"))  { ReflectionProbe c;  CS::load(src["ReflectionProbe"], c);  s.add(e, std::move(c)); }
    if (src.contains("Animation"))    { Animation c;       CS::load(src["Animation"], c);       s.add(e, std::move(c)); }
    if (src.contains("Script"))       { ScriptComponent c; CS::load(src["Script"], c);          s.add(e, std::move(c)); }
    if (src.contains("UICanvas"))     { UICanvas c;        CS::load(src["UICanvas"], c);        s.add(e, std::move(c)); }
    if (src.contains("UIElement"))    { UIElement c;       CS::load(src["UIElement"], c);       s.add(e, std::move(c)); }
    if (src.contains("UIImage"))      { UIImage c;         CS::load(src["UIImage"], c);         s.add(e, std::move(c)); }
    if (src.contains("UIText"))       { UIText c;          CS::load(src["UIText"], c);          s.add(e, std::move(c)); }
    if (src.contains("UIButton"))     { UIButton c;        CS::load(src["UIButton"], c);        s.add(e, std::move(c)); }
}

namespace {

/**
 * @brief Is @p id inside (but not the root of) a prefab instance?
 *
 * Walks up rather than marking every descendant, so the prefab's own entities
 * carry no bookkeeping and cannot fall out of sync with their root.
 */
bool isInsidePrefabInstance(const Scene& scene, EntityId id) {
    if (!scene.has<Hierarchy>(id)) return false;

    EntityId cursor = scene.get<Hierarchy>(id).parent;
    for (int depth = 0; depth < 32 && scene.isAlive(cursor); ++depth) {
        if (scene.has<PrefabInstance>(cursor)) return true;
        if (!scene.has<Hierarchy>(cursor)) break;
        cursor = scene.get<Hierarchy>(cursor).parent;
    }
    return false;
}

bool isKnownComponentKey(const std::string& k) {
    for (const char* key : COMPONENT_KEYS) {
        if (k == key) return true;
    }
    return false;
}

/**
 * @brief Build the full scene document (version + assets + entities +
 *        environment). Shared by save() (writes a file) and saveToString()
 *        (keeps it in memory for the play-mode snapshot).
 */
json buildSceneJson(const Scene& scene, const ResourceManager& resources) {
    json doc;
    doc["version"]  = FILE_FORMAT_VERSION;
    doc["assets"]   = AssetSerializer::saveAssetsForScene(scene, resources);
    doc["entities"] = json::array();

    scene.forEachEntity([&](EntityId id) {
        // Entities inside a prefab instance are not the scene's to describe -
        // the prefab file defines them, and the loader rebuilds them from it.
        if (isInsidePrefabInstance(scene, id)) return;

        json entity;
        entity["id"] = id.index;
        json components = json::object();

        // WorldTransform is derived from Transform + Hierarchy each frame -
        // not in the component list, not persisted.
        saveComponents(scene, id, components, resources);

        // A prefab root stores its source instead of its contents. Transform
        // and Hierarchy stay: where the instance sits, and what it hangs off,
        // belong to the scene rather than to the prefab.
        if (scene.has<PrefabInstance>(id)) {
            const json transform  = std::move(components["Transform"]);
            const json hierarchy  = std::move(components["Hierarchy"]);
            components = json::object();
            if (!transform.is_null()) components["Transform"] = std::move(transform);
            if (!hierarchy.is_null()) components["Hierarchy"] = std::move(hierarchy);
            entity["prefab"] = scene.get<PrefabInstance>(id).source;
        }

        entity["components"] = std::move(components);
        doc["entities"].push_back(std::move(entity));
    });

    // Scene-global settings: the lighting environment and the physics world, a
    // top-level object, not a per-entity component. Fully reflected - the
    // field list lives once, in environment.h.
    doc["environment"] = ComponentSerializer::save(scene.environment());
    doc["physics"]     = ComponentSerializer::save(scene.physics());
    return doc;
}

/**
 * @brief Validate + deserialize a scene document into @p scene + @p resources,
 *        committing atomically via swap. Shared by load() (from a file) and
 *        loadFromString() (from the play-mode snapshot); @p source labels the
 *        origin in log messages.
 *
 * @return true on success; false (and a logged error) leaves both untouched.
 */
bool readSceneJson(const json& doc, Scene& scene, ResourceManager& resources, const char* source) {
    const int version = doc.value("version", 0);
    if (version <= 0) {
        LOG_ERROR("Missing/invalid 'version' field in '%s'", source);
        return false;
    }
    if (version > FILE_FORMAT_VERSION) {
        LOG_ERROR("'%s' version %d is newer than this build (%d); refusing to load",
            source, version, FILE_FORMAT_VERSION);
        return false;
    }
    if (!doc.contains("entities") || !doc["entities"].is_array()) {
        LOG_ERROR("Missing or invalid 'entities' array in '%s'", source);
        return false;
    }

    // Transactional load: build both a staging Scene and a staging
    // ResourceManager. Asset factories that re-create from descriptors
    // (textures, materials, meshes) write into the staging RM, so a
    // failure mid-load leaves the live asset graph untouched. On full
    // success both swap into place in one commit phase.
    Scene staging;
    ResourceManager stagingResources;

    if (doc.contains("assets")) {
        // Inside a guard: a malformed assets block (bad JSON, missing library
        // entry) must log and leave the live scene + assets untouched, not throw
        // out of load(). The staging RM is discarded on the early return.
        try {
            AssetSerializer::loadAssets(doc["assets"], stagingResources);
        } catch (const std::exception& e) {
            LOG_ERROR("Asset load failed for '%s': %s - scene not loaded", source, e.what());
            return false;
        }
    }

    // Pass 1: create each entity at its saved slot index and populate
    // non-relational components. Hierarchy::parent is captured for pass 2
    // because the parent might not have been created yet on first sight.
    std::vector<std::pair<uint32_t, uint32_t>> parentLinks;  // (child idx, parent idx)
    std::vector<std::pair<Entity, std::string>> prefabRoots;  // instance roots to expand
    size_t entityCount = 0;
    std::set<std::string> unknownKeys;  // dedup warnings - one per drift, not per entity
    const json noComponents = json::object();   // stand-in for an entity that has none

    try {
        for (const auto& entry : doc["entities"]) {
            const uint32_t id = entry.value("id", 0u);
            if (id == 0) {
                LOG_WARNING("Entity with id=0 skipped (slot 0 reserved)");
                continue;
            }
            const Entity entity = staging.createEntityAt(id);
            ++entityCount;

            // Referenced, not value()'d: nlohmann returns by value, so asking
            // that way deep-copied every entity's whole component block on the
            // way past it.
            const auto it = entry.find("components");
            const json& components = (it != entry.end()) ? *it : noComponents;

            // Run every component's loader. Hierarchy is intentionally skipped
            // here - its parent index is captured below for the pass-2 wire-up.
            // Components that reference assets (Mesh) look them up in the
            // staging RM so resolution sees what loadAssets just built.
            loadComponents(components, staging, entity, stagingResources);
            if (components.contains("Hierarchy")) {
                const uint32_t parentIdx = CS::loadParentIndex(components["Hierarchy"]);
                if (parentIdx != std::numeric_limits<uint32_t>::max() && parentIdx != 0) {
                    parentLinks.emplace_back(id, parentIdx);
                }
            }

            if (entry.contains("prefab")) {
                prefabRoots.emplace_back(entity, entry.value("prefab", std::string{}));
                staging.add(entity, PrefabInstance{entry.value("prefab", std::string{})});
            }

            if (components.is_object()) {
                for (const auto& kv : components.items()) {
                    if (!isKnownComponentKey(kv.key())) unknownKeys.insert(kv.key());
                }
            }
        }

        // Pass 2b: expand prefab instances. After the entity pass so the roots
        // hold their saved slots, and the prefab's own entities take whatever
        // is free rather than competing for them.
        for (const auto& [root, prefabPath] : prefabRoots) {
            if (!Prefab::instantiateInto(staging, stagingResources, prefabPath, root)) {
                LOG_WARNING("Prefab '%s' failed to expand in '%s'; instance left empty",
                    prefabPath.c_str(), source);
            }
        }

        // Pass 2: wire up Hierarchy::parent now that every entity exists at
        // its saved slot. setParent rebuilds firstChild/nextSibling/
        // prevSibling on both sides and marks WorldTransform dirty.
        for (const auto& [childIdx, parentIdx] : parentLinks) {
            if (!staging.isAliveAtIndex(parentIdx)) {
                LOG_WARNING("Parent slot %u not found in '%s'; entity %u left as root",
                    parentIdx, source, childIdx);
                continue;
            }
            const EntityId childId {childIdx,  staging.generationOf(childIdx)};
            const EntityId parentId{parentIdx, staging.generationOf(parentIdx)};
            HierarchyOperations::setParent(staging, childId, parentId);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Aborted while reading '%s': %s (live scene unchanged)",
            source, e.what());
        return false;
    }

    for (const std::string& k : unknownKeys) {
        LOG_WARNING("Unknown component key '%s' in '%s' (schema drift; dropped)",
            k.c_str(), source);
    }

    // Scene-global settings: two top-level objects, the lighting environment and
    // the physics world. Missing fields keep the staging scene's defaults.
    if (auto it = doc.find("environment"); it != doc.end() && it->is_object()) {
        ComponentSerializer::load(*it, staging.environment());
    }
    if (auto it = doc.find("physics"); it != doc.end() && it->is_object()) {
        ComponentSerializer::load(*it, staging.physics());
    }

    // Commit phase: both stagings swap into place in one step. Until this
    // point a throw would leave both `scene` and `resources` untouched -
    // the malformed-file-half-loads-state failure mode is gone for both
    // entities and assets. Compact the new live scene to reclaim sparse
    // capacity that grew/shrunk during the staging build.
    //
    // Outstanding handles into `resources` from before this call are
    // stale - editor panels that cached handles to hidden previews
    // (MaterialEditor preview meshes, AssetBrowser neutral material)
    // re-acquire on next use via findByName-or-addPrivate (O(1) now).
    //
    // Fonts are engine-owned (baked at startup) and never enter the scene
    // file, so the staging RM has no font slot. Swap it back from the
    // just-displaced live RM - without it every UIText silently loses its
    // font (resolved by name each frame) on every load. Safe because
    // FontAsset is self-contained - no handles into the slots that were
    // just replaced.
    scene.swap(staging);
    resources.swap(stagingResources);
    resources.swapSlot<FontAsset>(stagingResources);
    scene.compact();

    LOG_INFO("Loaded scene from '%s' (%zu entities, %zu hierarchy links)",
        source, entityCount, parentLinks.size());
    return true;
}

} // namespace

bool save(const Scene& scene, const ResourceManager& resources, const std::string& path) {
    PROFILE_SCOPE("SceneSerializer::save");
    const json doc = buildSceneJson(scene, resources);

    std::ofstream out(path);
    if (!out) {
        LOG_ERROR("Failed to open '%s' for writing", path.c_str());
        return false;
    }
    out << doc.dump(2);
    const auto& assets = doc["assets"];
    const size_t numTex = assets.contains("textures")  ? assets["textures"].size()  : 0;
    const size_t numMat = assets.contains("materials") ? assets["materials"].size() : 0;
    const size_t numMsh = assets.contains("meshes")    ? assets["meshes"].size()    : 0;
    LOG_INFO("Saved scene to '%s' (%zu entities, %zu texture(s) + %zu material(s) + %zu mesh(es) referenced)",
        path.c_str(), doc["entities"].size(), numTex, numMat, numMsh);
    return true;
}

bool load(Scene& scene, ResourceManager& resources, const std::string& path) {
    PROFILE_SCOPE("SceneSerializer::load");
    json doc;
    if (!detail::readJsonFile(path, doc, "Scene")) return false;

    return readSceneJson(doc, scene, resources, path.c_str());
}

std::string saveToString(const Scene& scene, const ResourceManager& resources) {
    PROFILE_SCOPE("SceneSerializer::saveToString");
    return buildSceneJson(scene, resources).dump();
}

bool loadFromString(const std::string& text, Scene& scene, ResourceManager& resources) {
    PROFILE_SCOPE("SceneSerializer::loadFromString");
    json doc;
    try {
        doc = json::parse(text);
    } catch (const std::exception& e) {
        LOG_ERROR("SceneSerializer::loadFromString JSON parse error: %s", e.what());
        return false;
    }

    return readSceneJson(doc, scene, resources, "<memory snapshot>");
}

} // namespace Engine::SceneSerializer
