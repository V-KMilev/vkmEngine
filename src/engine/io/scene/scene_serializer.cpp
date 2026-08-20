#define VKM_LOG_CATEGORY "IO"

#include "io/scene/scene_serializer.h"

#include <array>
#include <charconv>
#include <limits>
#include <set>
#include <string>
#include <system_error>
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
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "ecs/component/prefab/prefab_instance.h"

namespace Vkm::Engine::SceneSerializer {

namespace {

using nlohmann::json;
namespace CS = ComponentSerializer;

// Assets are name-only references resolved through the cooked asset library.
constexpr int FILE_FORMAT_VERSION = 2;

// The largest slot a scene file may name. The file still sizes the entity slot
// table - createEntityAt grows two vectors to reach whatever id it names, and
// every SparseSet that entity touches grows its sparse array to the same key -
// so this bounds that cost rather than taking the choice away: the worst a file
// can ask for is four million slots instead of the four billion the id field can
// spell. It also keeps every accepted id clear of SparseSet's EMPTY sentinel.
// Raising it is safe while the ceiling stays an allocation a machine can meet.
// Roughly 300x the benchmark scene.
constexpr uint32_t MAX_ENTITY_SLOT = 1u << 22;

/**
 * @brief Every component the scene format round-trips, one row each.
 *
 * P is a component whose save and load take only the component; R is one that
 * references assets by name, so both take the ResourceManager as well
 * (resolution happens against the staging RM on load).
 *
 * The key is written out rather than derived from the type name, because it is
 * the format: ScriptComponent is stored as "Script", and a stringified type
 * name would change that silently.
 *
 * Saving, loading and the known-key set all expand from this one list, so the
 * three cannot drift: a component saved but never loaded is silent round-trip
 * data loss, and the unknown-key warning cannot catch it - the key is known.
 *
 * Hierarchy is not a row: it is written by saveComponents explicitly and read
 * by the caller's pass 2, not by a loader.
 */
#define VKM_SCENE_COMPONENTS(P, R)              \
    P(Name,             "Name")                 \
    P(Transform,        "Transform")            \
    P(Camera,           "Camera")               \
    P(Light,            "Light")                \
    P(Rigidbody,        "Rigidbody")            \
    P(Collider,         "Collider")             \
    P(CharacterController, "CharacterController") \
    R(Mesh,             "Mesh")                 \
    R(LOD,              "LOD")                  \
    R(Decal,            "Decal")                \
    P(ParticleEmitter,  "ParticleEmitter")      \
    P(IrradianceVolume, "IrradianceVolume")     \
    P(ReflectionProbe,  "ReflectionProbe")      \
    P(Animation,        "Animation")            \
    R(Animator,         "Animator")             \
    P(ScriptComponent,  "Script")               \
    P(UICanvas,         "UICanvas")             \
    P(UIElement,        "UIElement")            \
    P(UIImage,          "UIImage")              \
    P(UIText,           "UIText")               \
    P(UIButton,         "UIButton")

// Every JSON key written by saveComponents, for unknown-key detection on load.
// Order is incidental here (membership test only).
#define VKM_SCENE_KEY(Type, Key) Key,
constexpr std::array COMPONENT_KEYS = { VKM_SCENE_COMPONENTS(VKM_SCENE_KEY, VKM_SCENE_KEY) "Hierarchy" };
#undef VKM_SCENE_KEY

/**
 * @brief Read one component from @p src, when @p key is present, into @p e.
 *
 * Overwrites the component the entity already carries rather than adding a
 * second one: a prefab instance root is loaded twice - once from the scene
 * block that placed it, once from the prefab file - and SparseSet::add on a key
 * it already holds appends a second dense entry that outlives the entity.
 *
 * @tparam T Component type to read.
 * @tparam Args Extra arguments this component's loader takes (a ResourceManager
 *              for the ones that reference assets by name).
 * @param src The entity's component block.
 * @param key JSON key the component is stored under.
 * @param s Scene receiving the component.
 * @param e The entity receiving the component.
 * @param args Forwarded to ComponentSerializer::load.
 */
template<typename T, typename... Args>
void loadInto(const json& src, const char* key, Scene& s, EntityId e, Args&&... args) {
    const auto it = src.find(key);
    if (it == src.end()) return;

    T component;
    CS::load(*it, component, std::forward<Args>(args)...);
    if (s.has<T>(e)) s.get<T>(e) = std::move(component);
    else             s.add(e, std::move(component));
}

} // namespace

#define VKM_SCENE_SAVE(Type, Key)   if (s.has<Type>(id)) c[Key] = CS::save(s.get<Type>(id));
#define VKM_SCENE_SAVE_R(Type, Key) if (s.has<Type>(id)) c[Key] = CS::save(s.get<Type>(id), r);

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    VKM_SCENE_COMPONENTS(VKM_SCENE_SAVE, VKM_SCENE_SAVE_R)

    // Written here, but read by the caller's second pass rather than by a
    // loader: the parent it names may not exist yet when this entity is read.
    if (s.has<Hierarchy>(id)) c["Hierarchy"] = CS::save(s.get<Hierarchy>(id));
}

#undef VKM_SCENE_SAVE
#undef VKM_SCENE_SAVE_R

#define VKM_SCENE_LOAD(Type, Key)   loadInto<Type>(src, Key, s, e);
#define VKM_SCENE_LOAD_R(Type, Key) loadInto<Type>(src, Key, s, e, r);

void loadComponents(const json& src, Scene& s, EntityId e, const ResourceManager& r) {
    VKM_SCENE_COMPONENTS(VKM_SCENE_LOAD, VKM_SCENE_LOAD_R)
}

#undef VKM_SCENE_LOAD
#undef VKM_SCENE_LOAD_R

namespace {

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
        if (Prefab::isInsideInstance(scene, id)) return;

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
            json transform = std::move(components["Transform"]);
            json hierarchy = std::move(components["Hierarchy"]);
            components = json::object();
            if (!transform.is_null()) components["Transform"] = std::move(transform);
            if (!hierarchy.is_null()) components["Hierarchy"] = std::move(hierarchy);
            const PrefabInstance& instance = scene.get<PrefabInstance>(id);
            entity["prefab"] = instance.source;

            // uid -> component -> field. The nesting is the address, and object
            // keys make a duplicate (uid, component, field) unrepresentable.
            // Omitted when empty, so an instance with no edits reads exactly as
            // it did before overrides existed.
            if (!instance.overrides.empty()) {
                json overrides = json::object();
                for (const PrefabOverride& o : instance.overrides) {
                    json value;
                    try {
                        value = json::parse(o.value);
                    } catch (const std::exception&) {
                        continue;  // read-side already rejected these; belt and braces
                    }
                    overrides[std::to_string(o.uid)][o.component][o.field] = std::move(value);
                }
                if (!overrides.empty()) entity["overrides"] = std::move(overrides);
            }
        }

        entity["components"] = std::move(components);
        doc["entities"].push_back(std::move(entity));
    });

    // Scene-global settings: top-level objects, not per-entity components.
    // Fully reflected - the field list lives once, in environment.h.
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

    // Transactional load: the asset factories write into the staging
    // ResourceManager and the entities into the staging Scene, so a failure
    // mid-load leaves the live scene and asset graph untouched.
    Scene staging;
    ResourceManager stagingResources;

    if (doc.contains("assets")) {
        // Inside a guard: a malformed assets block (bad JSON, missing library
        // entry) must log and leave the live scene + assets untouched, not throw
        // out of load().
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
    std::vector<EntityId> prefabRoots;  // instance roots to expand
    size_t entityCount = 0;
    size_t unusableIds = 0;   // tallied, not logged per entry, like unknownKeys below
    size_t duplicateIds = 0;
    std::set<std::string> unknownKeys;  // dedup warnings - one per drift, not per entity
    const json noComponents = json::object();   // stand-in for an entity that has none

    try {
        for (const auto& entry : doc["entities"]) {
            const uint32_t id = entry.value("id", 0u);
            if (id == 0 || id > MAX_ENTITY_SLOT) {
                ++unusableIds;
                continue;
            }
            // A repeated id would allocate an already-live slot and add every
            // component to it twice: SparseSet appends a second dense entry
            // rather than overwriting, so the entity yields each component twice
            // and a later remove swap-and-pops against a stale index.
            if (staging.isAliveAtIndex(id)) {
                ++duplicateIds;
                continue;
            }
            const EntityId entity = staging.createEntityAt(id);
            ++entityCount;

            // Referenced, not value()'d: nlohmann returns by value, so asking
            // that way deep-copied every entity's whole component block on the
            // way past it.
            const auto it = entry.find("components");
            const json& components = (it != entry.end()) ? *it : noComponents;

            // Components that reference assets (Mesh) look them up in the
            // staging RM, so resolution sees what loadAssets just built.
            // Hierarchy is skipped: its parent index is captured below.
            loadComponents(components, staging, entity, stagingResources);
            if (components.contains("Hierarchy")) {
                const uint32_t parentIdx = CS::loadParentIndex(components["Hierarchy"]);
                if (parentIdx != std::numeric_limits<uint32_t>::max() && parentIdx != 0) {
                    parentLinks.emplace_back(id, parentIdx);
                }
            }

            if (entry.contains("prefab")) {
                PrefabInstance instance;
                instance.source = entry.value("prefab", std::string{});

                // Flatten uid -> component -> field back into the stored list.
                // A value that will not parse is the one drift case that drops:
                // it cannot be held in memory as text we could write back, and
                // it can only come from a hand-edit.
                if (entry.contains("overrides") && entry["overrides"].is_object()) {
                    for (const auto& [uidKey, comps] : entry["overrides"].items()) {
                        if (!comps.is_object()) continue;

                        // The key is the address, so a key that is not a uid
                        // addresses nothing. Parsed whole rather than as far as
                        // it goes: strtoul's answer for "head" is 0, which is
                        // the root, and the override would land there.
                        uint32_t uid = 0;
                        const char* last = uidKey.data() + uidKey.size();
                        const auto [stop, ec] = std::from_chars(uidKey.data(), last, uid);
                        if (ec != std::errc{} || stop != last) {
                            LOG_WARNING("Override key '%s' in '%s' is not an entity uid; dropped",
                                uidKey.c_str(), source);
                            continue;
                        }

                        for (const auto& [comp, fields] : comps.items()) {
                            if (!fields.is_object()) continue;
                            for (const auto& [field, value] : fields.items()) {
                                instance.overrides.push_back(
                                    PrefabOverride{uid, comp, field, value.dump()});
                            }
                        }
                    }
                }

                prefabRoots.push_back(entity);
                staging.add(entity, std::move(instance));
            }

            if (components.is_object()) {
                for (const auto& kv : components.items()) {
                    if (!isKnownComponentKey(kv.key())) unknownKeys.insert(kv.key());
                }
            }
        }

        if (unusableIds > 0) {
            LOG_WARNING("%zu entity record(s) in '%s' skipped: id 0 is the reserved slot and "
                "an id above %u is not one this build will size the slot table to",
                unusableIds, source, MAX_ENTITY_SLOT);
        }
        if (duplicateIds > 0) {
            LOG_WARNING("%zu entity record(s) in '%s' skipped: the id was already taken by an "
                "earlier record", duplicateIds, source);
        }

        // Pass 2b: expand prefab instances. After the entity pass so the roots
        // hold their saved slots, and the prefab's own entities take whatever
        // is free rather than competing for them.
        std::set<std::string> prefabDrift;
        for (const EntityId root : prefabRoots) {
            // Held across the expansion, which is safe because nothing it does
            // adds a PrefabInstance: the prefab's own entities carry
            // PrefabEntity, and nesting is refused at save time.
            const PrefabInstance& instance = staging.get<PrefabInstance>(root);
            if (!Prefab::instantiateInto(staging, stagingResources, instance.source, root,
                                         instance.overrides, &prefabDrift)) {
                LOG_WARNING("Prefab '%s' failed to expand in '%s'; instance left empty",
                    instance.source.c_str(), source);
            }
        }
        for (const std::string& message : prefabDrift) {
            LOG_WARNING("%s (kept, not applied)", message.c_str());
        }

        // Pass 2: wire up Hierarchy::parent now that every entity exists at its
        // saved slot. setParent rebuilds the sibling links on both sides and
        // seeds the WorldTransform the first HierarchySystem tick fills in.
        for (const auto& [childIdx, parentIdx] : parentLinks) {
            if (!staging.isAliveAtIndex(parentIdx)) {
                LOG_WARNING("Parent slot %u not found in '%s'; entity %u left as root",
                    parentIdx, source, childIdx);
                continue;
            }
            const EntityId childId  = staging.entityAt(childIdx);
            const EntityId parentId = staging.entityAt(parentIdx);
            HierarchyOperations::setParent(staging, childId, parentId);
        }

        // Missing scene-global fields keep the staging scene's defaults; a
        // mistyped one throws, and is caught here like any other malformed
        // block rather than unwinding out of load().
        if (auto it = doc.find("environment"); it != doc.end() && it->is_object()) {
            ComponentSerializer::load(*it, staging.environment());
        }
        if (auto it = doc.find("physics"); it != doc.end() && it->is_object()) {
            ComponentSerializer::load(*it, staging.physics());
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

    // Both stagings swap in one step; compact() reclaims the sparse capacity the
    // staging build grew.
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

    if (!detail::writeJsonFile(path, doc, "Scene")) return false;

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

} // namespace Vkm::Engine::SceneSerializer
