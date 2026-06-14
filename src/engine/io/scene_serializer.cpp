#define VKM_LOG_CATEGORY "IO"

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

#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/entity.h"
#include "io/asset_serializer.h"
#include "io/component_serializer.h"
#include "resource/resource_manager.h"
#include "resource/asset/shader_asset.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine::SceneSerializer {

namespace {

using nlohmann::json;
namespace CS = ComponentSerializer;

constexpr int FILE_FORMAT_VERSION = 1;

/**
 * @brief Per-component serialization recipe.
 *
 * Each saveable component has a SerializerTraits specialisation providing
 * a JSON key + save / load statics. The two macros below stamp out the
 * common shapes so this file stays a flat list of component names:
 *
 *   VKM_SERIALIZER_TRAITS(Type, "Key")        - default shape; CS::save(v)
 *   VKM_SERIALIZER_TRAITS_R(Type, "Key")      - takes ResourceManager&;
 *                                               used by components that
 *                                               reference assets by handle
 *                                               (Mesh, today)
 *
 * Save-only types (Hierarchy: collects parent links in pass 1, wires them
 * up in pass 2) declare the specialisation by hand without a `load` static;
 * the HasLoad<T> detector in this file skips them during pass 1.
 */
template<typename T> struct SerializerTraits;  // primary; defined via specialisations below.

#define VKM_SERIALIZER_TRAITS(Type, JsonKey)                                                  \
    template<> struct SerializerTraits<Type> {                                                \
        static constexpr const char* key = JsonKey;                                           \
        static json save(const Type& v, const ResourceManager&) { return CS::save(v); }       \
        static void load(const json& j, Type& v, ResourceManager&) { CS::load(j, v); }        \
    }

#define VKM_SERIALIZER_TRAITS_R(Type, JsonKey)                                                \
    template<> struct SerializerTraits<Type> {                                                \
        static constexpr const char* key = JsonKey;                                           \
        static json save(const Type& v, const ResourceManager& r) { return CS::save(v, r); }  \
        static void load(const json& j, Type& v, ResourceManager& r) { CS::load(j, v, r); }   \
    }

VKM_SERIALIZER_TRAITS  (Name,              "Name");
VKM_SERIALIZER_TRAITS  (Transform,         "Transform");
VKM_SERIALIZER_TRAITS  (Camera,            "Camera");
VKM_SERIALIZER_TRAITS  (Light,             "Light");
VKM_SERIALIZER_TRAITS  (Rigidbody,         "Rigidbody");
VKM_SERIALIZER_TRAITS  (Collider,          "Collider");
VKM_SERIALIZER_TRAITS  (PhysicsWorld,      "PhysicsWorld");
VKM_SERIALIZER_TRAITS_R(Mesh,              "Mesh");
VKM_SERIALIZER_TRAITS  (Animation,         "Animation");
VKM_SERIALIZER_TRAITS  (ScriptComponent,   "Script");

template<> struct SerializerTraits<Hierarchy> {
    static constexpr const char* key = "Hierarchy";
    static json save(const Hierarchy& v, const ResourceManager&) { return CS::save(v); }
    // No `load` - Hierarchy collects parent links in pass 1 and wires them
    // up in pass 2 via HierarchyOperations::setParent; the entity has to
    // exist before its parent slot can be resolved.
};

#undef VKM_SERIALIZER_TRAITS
#undef VKM_SERIALIZER_TRAITS_R

/// Compile-time list of every component type that serializes. Adding a
/// component = add a SerializerTraits specialisation above and add the type
/// here. The fold operators below propagate the change to save / load /
/// known-key checks; no other edits required.
using SerializedComponents = std::tuple<
    Name, Transform, Camera, Light, Rigidbody, Collider, PhysicsWorld, Mesh, Animation, ScriptComponent, Hierarchy
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
        json entity;
        entity["id"] = id.index;
        json components = json::object();

        // WorldTransform is derived from Transform + Hierarchy each frame -
        // not in SerializedComponents, not persisted.
        saveAll(scene, id, components, resources, static_cast<SerializedComponents*>(nullptr));

        entity["components"] = std::move(components);
        doc["entities"].push_back(std::move(entity));
    });

    // Scene-global lighting environment (skybox + IBL): a top-level object, not
    // a per-entity component.
    const Environment& env = scene.environment();
    doc["environment"] = {
        {"hdrPath",    env.hdrPath},
        {"intensity",  env.intensity},
        {"showSkybox", env.showSkybox},
    };
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
    if (version < FILE_FORMAT_VERSION) {
        // Older files load on a best-effort basis: every per-component load()
        // uses json.value("key", fallback) so missing fields keep their
        // struct defaults. Migrations live here when fields are renamed or
        // their meaning changes (no schema migrations needed at v1).
        LOG_INFO("'%s' version %d, current is %d (loading with defaults for missing fields)",
            source, version, FILE_FORMAT_VERSION);
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
                LOG_WARNING("Entity with id=0 skipped (slot 0 reserved)");
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

    // Scene-global lighting environment (skybox + IBL): a top-level object. A
    // file saved before it existed just keeps the staging scene's defaults.
    if (auto it = doc.find("environment"); it != doc.end() && it->is_object()) {
        Environment& env = staging.environment();
        env.hdrPath    = it->value("hdrPath",    env.hdrPath);
        env.intensity  = it->value("intensity",  env.intensity);
        env.showSkybox = it->value("showSkybox", env.showSkybox);
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
    // Shaders are engine-owned (loaded once in main()) and never enter the
    // scene file, so the staging RM has no ShaderAsset slot. Swap shaders
    // back from the just-displaced live RM so cached shader handles in the
    // render passes stay valid; without this the next frame's draw
    // dereferences an empty ShaderAsset slot and segfaults.
    scene.swap(staging);
    resources.swap(stagingResources);
    resources.swapSlot<ShaderAsset>(stagingResources);
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
