#include "io/scene_serializer.h"

#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/entity.h"
#include "io/asset_serializer.h"
#include "io/component_serializer.h"
#include "resource/resource_manager.h"
#include "resource/shader_asset.h"
#include "system/render/render_view.h"   // EnvironmentConfig
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine::SceneSerializer {

namespace {

using nlohmann::json;
namespace CS = ComponentSerializer;

constexpr int FILE_FORMAT_VERSION = 1;

/**
 * @brief Per-component serialization recipe.
 *
 * Adding a new component is a single specialization: the JSON key, a `save`
 * static, and a `load` static (or omit `load` for hierarchy-style two-pass
 * components - the SaveLoadFor<T>::hasLoad fold will skip them in pass 1).
 *
 * For "uses ResourceManager" components (just Mesh today), the static
 * signatures take a `const ResourceManager&` / `ResourceManager&`. For all
 * others the parameter is ignored; the trait fold passes it unconditionally.
 */
template<typename T> struct SerializerTraits;  // primary; defined via specialisations below.

template<> struct SerializerTraits<Name> {
    static constexpr const char* key = "Name";
    static json save(const Name& v, const ResourceManager&) { return CS::save(v); }
    static void load(const json& j, Name& v, ResourceManager&) { CS::load(j, v); }
};
template<> struct SerializerTraits<Transform> {
    static constexpr const char* key = "Transform";
    static json save(const Transform& v, const ResourceManager&) { return CS::save(v); }
    static void load(const json& j, Transform& v, ResourceManager&) { CS::load(j, v); }
};
template<> struct SerializerTraits<Camera> {
    static constexpr const char* key = "Camera";
    static json save(const Camera& v, const ResourceManager&) { return CS::save(v); }
    static void load(const json& j, Camera& v, ResourceManager&) { CS::load(j, v); }
};
template<> struct SerializerTraits<Light> {
    static constexpr const char* key = "Light";
    static json save(const Light& v, const ResourceManager&) { return CS::save(v); }
    static void load(const json& j, Light& v, ResourceManager&) { CS::load(j, v); }
};
template<> struct SerializerTraits<Mesh> {
    static constexpr const char* key = "Mesh";
    static json save(const Mesh& v, const ResourceManager& r) { return CS::save(v, r); }
    static void load(const json& j, Mesh& v, ResourceManager& r) { CS::load(j, v, r); }
};
template<> struct SerializerTraits<Animation> {
    static constexpr const char* key = "Animation";
    static json save(const Animation& v, const ResourceManager&) { return CS::save(v); }
    static void load(const json& j, Animation& v, ResourceManager&) { CS::load(j, v); }
};
template<> struct SerializerTraits<EnvironmentConfig> {
    static constexpr const char* key = "Environment";
    static json save(const EnvironmentConfig& v, const ResourceManager&) { return CS::save(v); }
    static void load(const json& j, EnvironmentConfig& v, ResourceManager&) { CS::load(j, v); }
};
template<> struct SerializerTraits<Hierarchy> {
    static constexpr const char* key = "Hierarchy";
    static json save(const Hierarchy& v, const ResourceManager&) { return CS::save(v); }
    // No `load` - Hierarchy collects parent links in pass 1 and wires them
    // up in pass 2 via HierarchyOperations::setParent; the entity has to
    // exist before its parent slot can be resolved.
};

/// Compile-time list of every component type that serializes. Adding a
/// component = add a SerializerTraits specialisation above and add the type
/// here. The fold operators below propagate the change to save / load /
/// known-key checks; no other edits required.
using SerializedComponents = std::tuple<
    Name, Transform, Camera, Light, Mesh, Animation, EnvironmentConfig, Hierarchy
>;

// Detect at compile time which traits expose a `load` static. Hierarchy
// opts out; everyone else opts in.
template<typename T, typename = void>
struct HasLoad : std::false_type {};
template<typename T>
struct HasLoad<T, std::void_t<decltype(SerializerTraits<T>::load(
    std::declval<const json&>(), std::declval<T&>(), std::declval<ResourceManager&>()
))>> : std::true_type {};

template<typename T>
void saveOne(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    if (s.has<T>(id)) c[SerializerTraits<T>::key] = SerializerTraits<T>::save(s.get<T>(id), r);
}
template<typename T>
void loadOne(Scene& s, Entity e, const json& src, ResourceManager& r) {
    if constexpr (HasLoad<T>::value) {
        const char* key = SerializerTraits<T>::key;
        if (!src.contains(key)) return;
        T value;
        SerializerTraits<T>::load(src[key], value, r);
        s.add(e, std::move(value));
    }
}

template<typename... Ts>
void saveAll(const Scene& s, EntityId id, json& c, const ResourceManager& r, std::tuple<Ts...>*) {
    (saveOne<Ts>(s, id, c, r), ...);
}
template<typename... Ts>
void loadAll(Scene& s, Entity e, const json& src, ResourceManager& r, std::tuple<Ts...>*) {
    (loadOne<Ts>(s, e, src, r), ...);
}
template<typename... Ts>
bool isKnownKey(const std::string& k, std::tuple<Ts...>*) {
    return ((k == SerializerTraits<Ts>::key) || ...);
}

bool isKnownComponentKey(const std::string& k) {
    return isKnownKey(k, static_cast<SerializedComponents*>(nullptr));
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
        // not in SerializedComponents, not persisted.
        saveAll(scene, id, components, resources, static_cast<SerializedComponents*>(nullptr));

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

    // Transactional load: build both a staging Scene and a staging
    // ResourceManager. Asset factories that re-create from descriptors
    // (textures, materials, meshes) write into the staging RM, so a
    // failure mid-load leaves the live asset graph untouched. On full
    // success both swap into place in one commit phase.
    Scene staging;
    ResourceManager stagingResources;

    if (doc.contains("assets")) {
        AssetSerializer::loadAssets(doc["assets"], stagingResources);
    }

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

            // Run every component's loader via the trait fold. Hierarchy is
            // intentionally skipped here (no `load` static in its traits) -
            // its parent index is captured below for the pass-2 wire-up.
            // Components that reference assets (Mesh) look them up in the
            // staging RM so resolution sees what loadAssets just built.
            loadAll(staging, entity, components, stagingResources,
                static_cast<SerializedComponents*>(nullptr));
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

    // Commit phase: both stagings swap into place in one step. Until this
    // point a throw would leave both `scene` and `resources` untouched -
    // the malformed-file-half-loads-state failure mode is gone for both
    // entities and assets. Compact the new live scene to reclaim sparse
    // capacity that grew/shrunk during the staging build.
    //
    // Outstanding handles into `resources` from before this call are
    // stale - editor panels that cached handles to editor-only previews
    // (MaterialEditor preview meshes, AssetBrowser neutral material)
    // re-acquire on next use via findByName-or-addInternal (O(1) now).
    //
    // Shaders are engine-owned (loaded once in main()) and never enter the
    // scene file, so the staging RM has no ShaderAsset slot. Swap shaders
    // back from the just-displaced live RM so cached shader handles in the
    // render passes stay valid; without this the next frame's IBL bake
    // dereferences an empty ShaderAsset slot and segfaults.
    scene.swap(staging);
    resources.swap(stagingResources);
    resources.swapSlot<ShaderAsset>(stagingResources);
    scene.compact();

    LOG_INFO("Loaded scene from '%s' (%zu entities, %zu hierarchy links)",
        path.c_str(), entityCount, parentLinks.size());
    return true;
}

} // namespace Engine::SceneSerializer
