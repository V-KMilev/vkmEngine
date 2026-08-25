#define VKM_LOG_CATEGORY "IO"

#include "io/scene/scene_serializer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/entity.h"
#include "ecs/component/core/missing_assets.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/component_serializer.h"
#include "system/physics/authoring/mesh_collider.h"
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

// Every JSON key written by saveComponents, for unknown-key detection on load.
// Order is incidental here (membership test only).
#define VKM_SCENE_KEY(Type, Key) Key,
constexpr std::array COMPONENT_KEYS = { VKM_SCENE_COMPONENTS(VKM_SCENE_KEY, VKM_SCENE_KEY) "Hierarchy" };
#undef VKM_SCENE_KEY

/**
 * @brief Move whatever the component just loaded could not resolve onto @p e.
 *
 * The loader knows the name it failed on; only here is it known whose it was
 * and which component it was read from, which is the address a save needs to
 * put it back. Called for every component, not only the ones that reference
 * assets, so the list is empty by the time the next one is read.
 *
 * @param s Scene holding the entity.
 * @param e Entity the component was read into.
 * @param key Scene-format key the component was stored under.
 */
void recordUnresolved(Scene& s, EntityId e, const char* key) {
    std::vector<CS::UnresolvedRef> missed = CS::takeUnresolvedRefs();
    if (missed.empty()) return;

    if (!s.has<MissingAssets>(e)) s.add(e, MissingAssets{});
    MissingAssets& record = s.get<MissingAssets>(e);
    for (CS::UnresolvedRef& ref : missed) {
        record.refs.push_back({key, std::move(ref.field), std::move(ref.name), ref.type});
    }
}

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

    // Bounds what this loader resolves to this component. A loader that throws
    // part-way is abandoned and the component is not added, so its names must
    // not be left for the next component read to record as its own.
    CS::UnresolvedScope unresolved;

    T component;
    try {
        CS::load(*it, component, std::forward<Args>(args)...);
    } catch (const std::exception& error) {
        // nlohmann names the type mismatch and nothing about where in the file
        // it happened. The key is in hand here and the entity id one level up,
        // so both are attached on the way out.
        throw std::runtime_error(std::string("component '") + key + "': " + error.what());
    }
    if (s.has<T>(e)) s.get<T>(e) = std::move(component);
    else             s.add(e, std::move(component));

    recordUnresolved(s, e, key);
}

} // namespace

#define VKM_SCENE_SAVE(Type, Key)   if (s.has<Type>(id)) c[Key] = CS::save(s.get<Type>(id));
#define VKM_SCENE_SAVE_R(Type, Key) if (s.has<Type>(id)) c[Key] = CS::save(s.get<Type>(id), r);

/**
 * @brief Write every component @p id carries into @p c, and nothing else.
 *
 * The entity exactly as the scene holds it now, which is what makes this the
 * one answer to "what is in that field". saveComponents puts the unresolved
 * names back on top of it; pruneResolvedRefs asks whether they are still
 * wanted.
 *
 * @param s Scene holding the entity.
 * @param id Entity to write.
 * @param c Object receiving one key per component.
 * @param r Asset graph, for the components that name assets.
 */
void writeComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    VKM_SCENE_COMPONENTS(VKM_SCENE_SAVE, VKM_SCENE_SAVE_R)

    // Written here, but read by the caller's second pass rather than by a
    // loader: the parent it names may not exist yet when this entity is read.
    if (s.has<Hierarchy>(id)) c["Hierarchy"] = CS::save(s.get<Hierarchy>(id));
}

#undef VKM_SCENE_SAVE
#undef VKM_SCENE_SAVE_R

/**
 * @brief Whether @p ref's field came out of writeComponents as an empty string.
 *
 * Which is the one state a kept name can go back into: a slot the author has
 * since filled keeps what they chose.
 *
 * @param c An entity's components, as writeComponents wrote them.
 * @param ref A reference the load could not resolve.
 * @return true if that field is present, a string, and empty.
 */
bool fieldLeftEmpty(const json& c, const MissingAssetRef& ref) {
    const auto component = c.find(ref.component);
    if (component == c.end()) return false;
    const auto field = component->find(ref.field);
    if (field == component->end()) return false;
    return field->is_string() && field->get<std::string>().empty();
}

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    writeComponents(s, id, c, r);

    // What the last load could not resolve goes back exactly as it came: the
    // write above put "" over the name, and an empty slot cannot be told from
    // one nobody ever filled (docs/reference/system/io.md).
    if (!s.has<MissingAssets>(id)) return;
    for (const MissingAssetRef& ref : s.get<MissingAssets>(id).refs) {
        if (fieldLeftEmpty(c, ref)) c[ref.component][ref.field] = ref.name;
    }
}

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

    // Here rather than in save(), so the play-mode snapshot is held to the same
    // rule as the file: a scene that cannot be written is one that cannot be
    // restored when the user presses Stop.
    detail::writeNonFiniteAsZero(doc, "Scene");
    return doc;
}

/**
 * @brief What a read does with the asset graph it is handed.
 */
enum class AssetPolicy {
    /**
     * @brief Build a replacement graph and swap it in.
     *
     * What a file the editor opens needs: the outgoing scene's assets go with
     * it rather than accumulating a scene's worth per open. Every handle issued
     * before the swap is stale afterwards.
     */
    Replace,
    /**
     * @brief Resolve against the live graph, adding only names it does not hold.
     *
     * What restoring the play snapshot needs. That document was serialized out
     * of this very graph moments earlier, so it names nothing the graph is
     * missing and nothing is created - and because no swap happens, every
     * handle issued before it still means what it meant. The editor's undo
     * history is the reason that matters: its steps hold the assets they are
     * to put back, and a swap turns those into keys into a manager that no
     * longer exists.
     */
    Merge
};

/**
 * @brief Validate + deserialize a scene document into @p scene + @p resources,
 *        committing the scene atomically via swap. Shared by load() (from a
 *        file) and loadFromString() (from the play-mode snapshot); @p source
 *        labels the origin in log messages.
 *
 * @param doc Scene document to read.
 * @param scene Scene to replace on success.
 * @param resources Asset graph to resolve against, and to replace under
 *        AssetPolicy::Replace.
 * @param source Origin, for log messages.
 * @param policy What to do with the asset graph; see AssetPolicy.
 * @return true on success; false (and a logged error) leaves both untouched.
 */
bool readSceneJson(const json& doc, Scene& scene, ResourceManager& resources, const char* source,
                   AssetPolicy policy) {
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

    // Transactional: the factories write into the staging ResourceManager and
    // the entities into the staging Scene, so a failure mid-load leaves the live
    // pair untouched. Merge has no staging graph to fail into - see AssetPolicy.
    Scene staging;
    ResourceManager stagingResources;
    ResourceManager& assetGraph = (policy == AssetPolicy::Merge) ? resources : stagingResources;

    if (doc.contains("assets")) {
        // Inside a guard: a malformed assets block (bad JSON, missing library
        // entry) must log and leave the live scene + assets untouched, not throw
        // out of load().
        try {
            AssetSerializer::loadAssets(doc["assets"], assetGraph);
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

    // Where the read is standing, for the catch below. The entity id is the
    // useful half and is zero outside the entity loop, where the block name is
    // all there is to say. Both are plain scalars rather than a formatted
    // string: this is updated once per entity on every load.
    uint32_t    entityBeingRead = 0;
    const char* blockBeingRead  = "entities";

    try {
        for (const auto& entry : doc["entities"]) {
            const uint32_t id = entry.value("id", 0u);
            entityBeingRead = id;
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
            // graph loadAssets just wrote into, so resolution sees it.
            // Hierarchy is skipped: its parent index is captured below.
            loadComponents(components, staging, entity, assetGraph);
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
            if (!Prefab::instantiateInto(staging, assetGraph, instance.source, root,
                                         instance.overrides, &prefabDrift)) {
                // The same seam an unresolved asset name goes through, and it
                // costs more: the whole authored subtree vanishes from the
                // viewport, leaving a childless entity behind.
                reportError("Scene", "prefab '" + instance.source + "'",
                    "could not be opened, so the instance is empty - the reference and "
                    "its overrides are kept, so restoring the file and loading again "
                    "brings the subtree back");
            }
        }
        for (const std::string& message : prefabDrift) {
            LOG_WARNING("%s (kept, not applied)", message.c_str());
        }

        entityBeingRead = 0;

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

        // A ragdoll's bones name the bodies that pose them, and those are
        // saved as slots for the same reason a joint's connected entity is.
        if (auto* ragdolls = staging.storage<Ragdoll>()) {
            for (uint32_t i = 0; i < ragdolls->size(); ++i) {
                Ragdoll& ragdoll = ragdolls->dataAt(i);
                const uint32_t rootSlot = ragdoll.root.index;
                ragdoll.root = rootSlot && staging.isAliveAtIndex(rootSlot)
                    ? staging.entityAt(rootSlot)
                    : EntityId{};

                for (RagdollBone& bone : ragdoll.bones) {
                    const uint32_t slot = bone.body.index;
                    if (slot == 0) continue;
                    if (!staging.isAliveAtIndex(slot)) {
                        LOG_WARNING("Ragdoll names body slot %u, which '%s' does "
                                    "not hold; that bone is left unsimulated",
                                    slot, source);
                        bone.body = EntityId{};
                        continue;
                    }
                    bone.body = staging.entityAt(slot);
                }
            }
        }

        // A joint names another entity, and a saved reference is a slot rather
        // than a handle: the generation it was written with belongs to the
        // session that wrote it. Recovered here, where every slot is filled, so
        // the joint holds a handle the scene will still recognise.
        if (auto* joints = staging.storage<Joint>()) {
            for (uint32_t i = 0; i < joints->size(); ++i) {
                Joint& joint = joints->dataAt(i);
                const uint32_t slot = joint.connected.index;
                if (slot == 0) continue;
                if (!staging.isAliveAtIndex(slot)) {
                    LOG_WARNING("Joint names slot %u, which '%s' does not hold; "
                                "the joint is left unconnected", slot, source);
                    joint.connected = EntityId{};
                    continue;
                }
                joint.connected = staging.entityAt(slot);
            }
        }

        // The tree over a mesh collider's triangles is derived, so it is not
        // written to disk where it could disagree with them. PhysicsSystem
        // rebuilds one it finds missing, but only on a tick - and a query is
        // not a tick, so a freshly loaded level answered nothing against its
        // own terrain until something moved. In the editor, where the
        // simulation is not running, that is never.
        if (auto* colliders = staging.storage<Collider>()) {
            for (uint32_t i = 0; i < colliders->size(); ++i) {
                rebuildMeshBvh(colliders->dataAt(i));
            }
        }

        // Missing scene-global fields keep the staging scene's defaults; a
        // mistyped one throws, and is caught here like any other malformed
        // block rather than unwinding out of load().
        blockBeingRead = "environment";
        if (auto it = doc.find("environment"); it != doc.end() && it->is_object()) {
            ComponentSerializer::load(*it, staging.environment());
        }
        blockBeingRead = "physics";
        if (auto it = doc.find("physics"); it != doc.end() && it->is_object()) {
            ComponentSerializer::load(*it, staging.physics());
        }
    } catch (const std::exception& e) {
        if (entityBeingRead != 0) {
            LOG_ERROR("Aborted while reading entity %u of '%s': %s (live scene unchanged)",
                entityBeingRead, source, e.what());
        } else {
            LOG_ERROR("Aborted while reading the %s block of '%s': %s (live scene unchanged)",
                blockBeingRead, source, e.what());
        }
        return false;
    }

    for (const std::string& k : unknownKeys) {
        LOG_WARNING("Unknown component key '%s' in '%s' (schema drift; dropped)",
            k.c_str(), source);
    }

    // Both stagings swap in one step; compact() reclaims the sparse capacity the
    // staging build grew. A Merge wrote into the live graph as it went, so it
    // has no second half to commit and nothing above it is stale.
    scene.swap(staging);
    if (policy == AssetPolicy::Replace) {
        resources.swap(stagingResources);
        // Fonts are baked at startup and never enter a scene file, so the
        // staging graph has no font slot to put in place of the live one.
        resources.swapSlot<FontAsset>(stagingResources);
    }
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

    return readSceneJson(doc, scene, resources, path.c_str(), AssetPolicy::Replace);
}

void pruneResolvedRefs(Scene& scene, const ResourceManager& resources, EntityId id) {
    if (!scene.isAlive(id) || !scene.has<MissingAssets>(id)) return;

    json components = json::object();
    writeComponents(scene, id, components, resources);

    std::vector<MissingAssetRef>& refs = scene.get<MissingAssets>(id).refs;
    refs.erase(std::remove_if(refs.begin(), refs.end(),
                   [&](const MissingAssetRef& ref) { return !fieldLeftEmpty(components, ref); }),
               refs.end());
    if (refs.empty()) scene.remove<MissingAssets>(id);
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

    return readSceneJson(doc, scene, resources, "<memory snapshot>", AssetPolicy::Merge);
}

} // namespace Vkm::Engine::SceneSerializer
