#include "io/scene_serializer.h"

#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/entity.h"
#include "io/asset_serializer.h"
#include "io/component_serializer.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"   // EnvironmentConfig
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine::SceneSerializer {

namespace {

using nlohmann::json;
namespace CS = ComponentSerializer;

constexpr int FILE_FORMAT_VERSION = 1;

/// One component type's save + load (+ JSON key) bundled together. The
/// scene loops over a table of these instead of repeating the same
/// has<T>/get<T>/CS::save / contains/CS::load/add pattern per type at
/// both ends - adding a new component becomes a single table entry, not
/// matched edits in scene save and scene load (and the known-key list).
struct ComponentEntry {
    const char* key;
    void (*save)(const Scene&, EntityId, json&, const ResourceManager&);
    /// `load` may be null for components whose load is structurally
    /// special (Hierarchy collects parent links in pass 1 and wires them
    /// up in pass 2; it can't be a simple has-and-add). The save side
    /// stays uniform; the load loop just skips null entries.
    void (*load)(Scene&, Entity, const json&, ResourceManager&);
};

void saveName       (const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<Name>(id))              c["Name"]        = CS::save(s.get<Name>(id)); }
void saveTransform  (const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<Transform>(id))         c["Transform"]   = CS::save(s.get<Transform>(id)); }
void saveCamera     (const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<Camera>(id))            c["Camera"]      = CS::save(s.get<Camera>(id)); }
void saveLight      (const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<Light>(id))             c["Light"]       = CS::save(s.get<Light>(id)); }
void saveMesh       (const Scene& s, EntityId id, json& c, const ResourceManager& r) { if (s.has<Mesh>(id))              c["Mesh"]        = CS::save(s.get<Mesh>(id), r); }
void saveHierarchy  (const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<Hierarchy>(id))         c["Hierarchy"]   = CS::save(s.get<Hierarchy>(id)); }
void saveAnimation  (const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<Animation>(id))         c["Animation"]   = CS::save(s.get<Animation>(id)); }
void saveEnvironment(const Scene& s, EntityId id, json& c, const ResourceManager&)   { if (s.has<EnvironmentConfig>(id)) c["Environment"] = CS::save(s.get<EnvironmentConfig>(id)); }

void loadName       (Scene& s, Entity e, const json& src, ResourceManager&)   { if (src.contains("Name"))        { Name c;              CS::load(src["Name"],        c);    s.add(e, std::move(c)); } }
void loadTransform  (Scene& s, Entity e, const json& src, ResourceManager&)   { if (src.contains("Transform"))   { Transform c;         CS::load(src["Transform"],   c);    s.add(e, std::move(c)); } }
void loadCamera     (Scene& s, Entity e, const json& src, ResourceManager&)   { if (src.contains("Camera"))      { Camera c;            CS::load(src["Camera"],      c);    s.add(e, std::move(c)); } }
void loadLight      (Scene& s, Entity e, const json& src, ResourceManager&)   { if (src.contains("Light"))       { Light c;             CS::load(src["Light"],       c);    s.add(e, std::move(c)); } }
void loadMesh       (Scene& s, Entity e, const json& src, ResourceManager& r) { if (src.contains("Mesh"))        { Mesh c;              CS::load(src["Mesh"],        c, r); s.add(e, std::move(c)); } }
void loadAnimation  (Scene& s, Entity e, const json& src, ResourceManager&)   { if (src.contains("Animation"))   { Animation a;         CS::load(src["Animation"],   a);    s.add(e, std::move(a)); } }
void loadEnvironment(Scene& s, Entity e, const json& src, ResourceManager&)   { if (src.contains("Environment")) { EnvironmentConfig c; CS::load(src["Environment"], c);    s.add(e, std::move(c)); } }
// Hierarchy load is two-pass (parent indices collected in pass 1, then
// setParent called in pass 2). Save fits the table; load is wired by hand
// in load() with a null entry below.

constexpr std::array<ComponentEntry, 8> kRegistry = {{
    {"Name",        saveName,        loadName},
    {"Transform",   saveTransform,   loadTransform},
    {"Camera",      saveCamera,      loadCamera},
    {"Light",       saveLight,       loadLight},
    {"Mesh",        saveMesh,        loadMesh},
    {"Animation",   saveAnimation,   loadAnimation},
    {"Environment", saveEnvironment, loadEnvironment},
    {"Hierarchy",   saveHierarchy,   nullptr},  // see load() for the pass-1/2 split
}};

bool isKnownComponentKey(const std::string& k) {
    for (const auto& e : kRegistry) if (k == e.key) return true;
    return false;
}

} // namespace

bool save(const Scene& scene, const ResourceManager& resources, const std::string& path) {
    json doc;
    doc["version"]  = FILE_FORMAT_VERSION;
    doc["assets"]   = AssetSerializer::saveAssetsForScene(scene, resources);
    doc["entities"] = json::array();

    scene.forEachEntity([&](EntityId id) {
        json entity;
        entity["id"] = id.index;
        json components = json::object();

        // WorldTransform is derived from Transform + Hierarchy each frame -
        // not in the registry, not persisted.
        for (const auto& reg : kRegistry) reg.save(scene, id, components, resources);

        entity["components"] = std::move(components);
        doc["entities"].push_back(std::move(entity));
    });

    std::ofstream out(path);
    if (!out) {
        LOG_ERROR("SceneSerializer::save failed to open '%s' for writing", path.c_str());
        return false;
    }
    out << doc.dump(2);
    LOG_INFO("Saved scene to '%s' (%zu entities)", path.c_str(), doc["entities"].size());
    return true;
}

bool load(Scene& scene, ResourceManager& resources, const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR("SceneSerializer::load failed to open '%s'", path.c_str());
        return false;
    }

    json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        LOG_ERROR("SceneSerializer::load JSON parse error in '%s': %s", path.c_str(), e.what());
        return false;
    }

    const int version = doc.value("version", 0);
    if (version <= 0) {
        LOG_ERROR("SceneSerializer::load: missing/invalid 'version' field");
        return false;
    }
    if (version > FILE_FORMAT_VERSION) {
        LOG_ERROR("SceneSerializer::load: file version %d is newer than this build (%d); refusing to load",
            version, FILE_FORMAT_VERSION);
        return false;
    }
    if (version < FILE_FORMAT_VERSION) {
        // Older files load on a best-effort basis: every per-component load()
        // uses json.value("key", fallback) so missing fields keep their
        // struct defaults. Migrations live here when fields are renamed or
        // their meaning changes (no schema migrations needed at v1).
        LOG_INFO("SceneSerializer::load: file version %d, current is %d (loading with defaults for missing fields)",
            version, FILE_FORMAT_VERSION);
    }
    if (!doc.contains("entities") || !doc["entities"].is_array()) {
        LOG_ERROR("SceneSerializer::load: missing or invalid 'entities' array");
        return false;
    }

    // Populate the asset graph before touching entities - Mesh components
    // will resolve their handles by name during entity load. Idempotent:
    // assets already present in ResourceManager are kept as-is.
    if (doc.contains("assets")) {
        AssetSerializer::loadAssets(doc["assets"], resources);
    }

    // Two-phase load: deserialise into a staging scene first. A throw or
    // unrecoverable error here is contained - the live scene is only
    // touched once the entire load has succeeded. Avoids the
    // "malformed-file-leaves-editor-empty" failure mode.
    Scene staging;

    // Pass 1: create each entity at its saved slot index and populate
    // non-relational components. Hierarchy::parent is captured for pass 2
    // because the parent might not have been created yet on first sight.
    std::vector<std::pair<uint32_t, uint32_t>> parentLinks;  // (child idx, parent idx)
    size_t entityCount = 0;
    std::set<std::string> unknownKeys;  // dedup warnings - one per drift, not per entity

    try {
        for (const auto& entry : doc["entities"]) {
            const uint32_t id = entry.value("id", 0u);
            if (id == 0) {
                LOG_WARNING("SceneSerializer::load: entity with id=0 skipped (slot 0 reserved)");
                continue;
            }
            const Entity entity = staging.createEntityAt(id);
            ++entityCount;

            const auto& components = entry.value("components", json::object());

            // Run every registered component's loader. Hierarchy has a null
            // load fn in the table because it needs two-pass treatment;
            // handle it explicitly below.
            for (const auto& reg : kRegistry) {
                if (reg.load) reg.load(staging, entity, components, resources);
            }
            if (components.contains("Hierarchy")) {
                const uint32_t parentIdx = CS::loadParentIndex(components["Hierarchy"]);
                if (parentIdx != std::numeric_limits<uint32_t>::max() && parentIdx != 0) {
                    parentLinks.emplace_back(id, parentIdx);
                }
            }

            if (components.is_object()) {
                for (const auto& kv : components.items()) {
                    if (!isKnownComponentKey(kv.key())) unknownKeys.insert(kv.key());
                }
            }
        }

        // Pass 2: wire up Hierarchy::parent now that every entity exists at
        // its saved slot. setParent rebuilds firstChild/nextSibling/
        // prevSibling on both sides and marks WorldTransform dirty.
        for (const auto& [childIdx, parentIdx] : parentLinks) {
            if (!staging.isAliveAtIndex(parentIdx)) {
                LOG_WARNING("SceneSerializer::load: parent slot %u not found in file; entity %u left as root",
                    parentIdx, childIdx);
                continue;
            }
            const EntityId childId {childIdx,  staging.generationOf(childIdx)};
            const EntityId parentId{parentIdx, staging.generationOf(parentIdx)};
            HierarchyOperations::setParent(staging, childId, parentId);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SceneSerializer::load: aborted while reading '%s': %s (live scene unchanged)",
            path.c_str(), e.what());
        return false;
    }

    for (const std::string& k : unknownKeys) {
        LOG_WARNING("SceneSerializer::load: unknown component key '%s' in '%s' (schema drift; dropped)",
            k.c_str(), path.c_str());
    }

    // Commit. swap moves staging's contents into the live scene; staging
    // now holds the previously-live entities and destroys them when it
    // goes out of scope. We deliberately don't call scene.clear() first -
    // that would walk the live entities entity-by-entity (each one
    // detachFromHierarchy + per-component remove) just to throw them away,
    // when staging's default destruction does the same memory release in
    // one pass per component storage.
    //
    // Asset state caveat: AssetSerializer::loadAssets() above mutates the
    // shared ResourceManager BEFORE this swap, so a failure during the
    // staging build (caught above) leaves any newly-loaded assets in
    // the resource graph as orphans. They're additive (no asset of the
    // same name is overwritten) so it's a leak, not corruption. A proper
    // transactional load would need an asset-graph swap mechanism;
    // tracked alongside the binary-scene-cache work.
    scene.swap(staging);

    // SparseSet capacity may have grown then shrunk during the staging
    // load - reclaim wasted slots so the loaded scene doesn't carry
    // orphan capacity.
    scene.compact();

    LOG_INFO("Loaded scene from '%s' (%zu entities, %zu hierarchy links)",
        path.c_str(), entityCount, parentLinks.size());
    return true;
}

} // namespace Engine::SceneSerializer
