#define VKM_LOG_CATEGORY "IO"

#include "io/scene/scene_serializer.h"

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

#include "core/memory/slot_allocator.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/entity.h"
#include "ecs/component/core/missing_assets.h"
#include "ecs/hierarchy_operations.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/component_serializer.h"
#include "io/json_file.h"
#include "resource/resource_manager.h"
#include "io/scene/prefab.h"
#include "ecs/component/prefab/prefab_instance.h"

namespace Vkm::Engine::SceneSerializer {

namespace {

// Load recreates each entity at its saved slot.
uint32_t sceneSlotName(EntityId e) { return e.slot(); }

using nlohmann::json;
namespace CS = ComponentSerializer;

// Bumped when the document layout changes. A file written by a newer build is refused.
constexpr int FILE_FORMAT_VERSION = 2;

// Every key saveComponents writes, for unknown-key detection on load.
#define VKM_SCENE_KEY(Type, Key) Key,
constexpr std::array COMPONENT_KEYS = {
    VKM_SCENE_COMPONENTS(VKM_SCENE_KEY, VKM_SCENE_KEY, VKM_SCENE_KEY) "Hierarchy"
};
#undef VKM_SCENE_KEY

/**
 * @brief Move whatever the component just loaded could not resolve onto @p e.
 *
 * Only here are entity and component known, the address a save needs. Called
 * for every component, so the list is empty before the next one is read.
 *
 * @param s Scene holding the entity.
 * @param e Entity the component was read into.
 * @param key Scene-format key the component was stored under.
 */
void recordUnresolved(Scene& s, EntityId e, const char* key) {
    std::vector<CS::UnresolvedRef> missed = CS::takeUnresolvedRefs();
    if (missed.empty()) return;

    MissingAssets* record = s.tryGet<MissingAssets>(e);
    if (!record) record = &s.add(e, MissingAssets{});
    for (CS::UnresolvedRef& ref : missed) {
        record->refs.push_back({key, std::move(ref.field), std::move(ref.name), ref.type});
    }
}

/**
 * @brief Read one component from @p src, when @p key is present, into @p e.
 *
 * Assigns over an existing component: a prefab instance root is read twice
 * (scene, then prefab file), and SparseSet::add would silently keep the first.
 *
 * @tparam T Component type to read.
 * @tparam Args Extra arguments this component's loader takes (a ResourceManager
 *              for an R row, an EntityResolver for an E row).
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

    // A loader that throws adds nothing, so its unresolved names must not leak
    // to the next component read.
    CS::UnresolvedScope unresolved;

    T component;
    try {
        CS::load(*it, component, std::forward<Args>(args)...);
    } catch (const std::exception& error) {
        // nlohmann says nothing of where; the key is added here, the entity one level up.
        throw std::runtime_error(std::string("component '") + key + "': " + error.what());
    }
    if (T* existing = s.tryGet<T>(e)) *existing = std::move(component);
    else                               s.add(e, std::move(component));

    recordUnresolved(s, e, key);
}

} // namespace

#define VKM_SCENE_SAVE(Type, Key)   if (const Type* v = s.tryGet<Type>(id)) c[Key] = CS::save(*v);
#define VKM_SCENE_SAVE_R(Type, Key) if (const Type* v = s.tryGet<Type>(id)) c[Key] = CS::save(*v, r);
#define VKM_SCENE_SAVE_E(Type, Key) if (const Type* v = s.tryGet<Type>(id)) c[Key] = CS::save(*v, name);

namespace {

/**
 * @brief Write every component @p id carries into @p c, and nothing else.
 *
 * @param s Scene holding the entity.
 * @param id Entity to write.
 * @param c Object receiving one key per component.
 * @param r Asset graph, for the components that name assets.
 * @param name Names the entities the components refer to.
 */
void writeComponents(
    const Scene& s,
    EntityId id,
    json& c,
    const ResourceManager& r,
    const EntityNamer& name
) {
    VKM_SCENE_COMPONENTS(VKM_SCENE_SAVE, VKM_SCENE_SAVE_R, VKM_SCENE_SAVE_E)

    // Read by the caller's second pass: the parent may not exist yet on load.
    if (const Hierarchy* h = s.tryGet<Hierarchy>(id)) c["Hierarchy"] = CS::save(*h);
}

#undef VKM_SCENE_SAVE
#undef VKM_SCENE_SAVE_R
#undef VKM_SCENE_SAVE_E

/**
 * @brief Whether @p ref's field came out of writeComponents as an empty string.
 *
 * Only then does a kept name go back; a slot filled since keeps its value.
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

} // namespace

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r, const EntityNamer& name) {
    writeComponents(s, id, c, r, name);

    // Unresolved names go back over the "" written above (docs/reference/io.md).
    const MissingAssets* missing = s.tryGet<MissingAssets>(id);
    if (!missing) return;
    for (const MissingAssetRef& ref : missing->refs) {
        if (fieldLeftEmpty(c, ref)) c[ref.component][ref.field] = ref.name;
    }
}

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    auto bySlot = sceneSlotName;
    const EntityNamer name(bySlot);
    saveComponents(s, id, c, r, name);
}

#define VKM_SCENE_LOAD(Type, Key)   loadInto<Type>(src, Key, s, e);
#define VKM_SCENE_LOAD_R(Type, Key) loadInto<Type>(src, Key, s, e, r);
#define VKM_SCENE_LOAD_E(Type, Key) loadInto<Type>(src, Key, s, e, resolve);

void loadComponents(
    const json& src,
    Scene& s,
    EntityId e,
    const ResourceManager& r,
    const EntityResolver& resolve
) {
    VKM_SCENE_COMPONENTS(VKM_SCENE_LOAD, VKM_SCENE_LOAD_R, VKM_SCENE_LOAD_E)
}

#undef VKM_SCENE_LOAD
#undef VKM_SCENE_LOAD_R
#undef VKM_SCENE_LOAD_E

namespace {

bool isKnownComponentKey(const std::string& k) {
    for (const char* key : COMPONENT_KEYS) {
        if (k == key) return true;
    }
    return false;
}

/**
 * @brief Every live entity, each root followed by its subtree in walk order.
 *
 * The loader re-attaches children in file order and setParent appends, so this
 * order is what reloads siblings in order. Breadth first per root with a
 * visited mark; entities no root reaches (a ring) follow in slot order.
 *
 * @param scene Scene to order.
 * @return Every live entity, once.
 */
std::vector<EntityId> entitiesInWalkOrder(const Scene& scene) {
    std::vector<EntityId> order;
    order.reserve(scene.entityCount());

    std::vector<bool> written;
    auto markWritten = [&](EntityId id) {
        if (id.slot() >= written.size()) written.resize(id.slot() + 1, false);
        if (written[id.slot()]) return false;
        written[id.slot()] = true;
        return true;
    };

    scene.forEachEntity([&](EntityId id) {
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        if (node && scene.isAlive(node->parent)) return;
        if (!markWritten(id)) return;

        const size_t first = order.size();
        order.push_back(id);
        for (size_t i = first; i < order.size(); ++i) {
            HierarchyOperations::forEachChild(scene, order[i], [&](EntityId child) {
                if (scene.isAlive(child) && markWritten(child)) order.push_back(child);
            });
        }
    });

    if (order.size() < scene.entityCount()) {
        scene.forEachEntity([&](EntityId id) {
            if (markWritten(id)) order.push_back(id);
        });
    }
    return order;
}

/**
 * @brief Build the full scene document: version, assets, entities, environment.
 *
 * @param scene     Scene to describe.
 * @param resources Resolves each handle the scene holds to the name written.
 * @return The document.
 */
json buildSceneJson(const Scene& scene, const ResourceManager& resources) {
    json doc;
    doc["version"]  = FILE_FORMAT_VERSION;
    doc["assets"]   = AssetSerializer::saveAssetsForScene(scene, resources);
    doc["entities"] = json::array();

    for (const EntityId id : entitiesInWalkOrder(scene)) {
        // The prefab file defines an instance's entities; the loader rebuilds them.
        if (Prefab::isInsideInstance(scene, id)) continue;

        json entity;
        entity["id"] = id.slot();
        json components = json::object();

        saveComponents(scene, id, components, resources);

        // A prefab root stores its source; Transform and Hierarchy stay as scene data.
        if (const PrefabInstance* instance = scene.tryGet<PrefabInstance>(id)) {
            json transform = std::move(components["Transform"]);
            json hierarchy = std::move(components["Hierarchy"]);
            components = json::object();
            if (!transform.is_null()) components["Transform"] = std::move(transform);
            if (!hierarchy.is_null()) components["Hierarchy"] = std::move(hierarchy);
            entity["prefab"] = instance->source;

            // uid -> component -> field: object keys make a duplicate unrepresentable.
            if (!instance->overrides.empty()) {
                json overrides = json::object();
                for (const PrefabOverride& o : instance->overrides) {
                    json value;
                    try {
                        value = json::parse(o.value);
                    } catch (const std::exception&) {
                        continue;  // not JSON, so not a value the file can hold
                    }
                    overrides[std::to_string(o.uid)][o.component][o.field] = std::move(value);
                }
                if (!overrides.empty()) entity["overrides"] = std::move(overrides);
            }
        }

        entity["components"] = std::move(components);
        doc["entities"].push_back(std::move(entity));
    }

    // Scene-global settings: top-level objects, not per-entity components.
    doc["environment"] = ComponentSerializer::save(scene.environment());
    doc["physics"]     = ComponentSerializer::save(scene.physics());
    return doc;
}

/**
 * @brief What a read does with the asset graph it is handed.
 */
enum class AssetPolicy {
    /**
     * @brief Build a replacement graph and swap it in.
     *
     * The outgoing scene's assets go with it; every earlier handle goes stale.
     */
    Replace,
    /**
     * @brief Resolve against the live graph, adding only names it does not hold.
     *
     * For a saveToString document from this graph; no swap, so handles stay valid.
     */
    Merge
};

/**
 * @brief Validate and deserialize a scene document into @p scene + @p
 *        resources, committing the scene atomically via swap.
 *
 * @param doc Scene document to read.
 * @param scene Scene to replace on success.
 * @param resources Asset graph to resolve against, and to replace under
 *        AssetPolicy::Replace.
 * @param source Origin, for log messages.
 * @param policy What to do with the asset graph; see AssetPolicy.
 * @param instanceSlots Where each instance's own entities stood, or null to
 *        build them into whatever is free.
 * @return true on success; false (logged) leaves the scene untouched, and the
 *         asset graph too under AssetPolicy::Replace.
 */
bool readSceneJson(
    const json& doc,
    Scene& scene,
    ResourceManager& resources,
    const char* source,
    AssetPolicy policy,
    const Prefab::InstanceSlots* instanceSlots
) {
    // Not value(), which throws on a string or array; a malformed version reads as missing.
    const int version = doc.contains("version") && doc["version"].is_number_integer()
        ? doc["version"].get<int>()
        : 0;
    if (version <= 0) {
        LOG_ERROR("Missing/invalid 'version' field in '%s'", source);
        return false;
    }
    if (version > FILE_FORMAT_VERSION) {
        LOG_ERROR(
            "'%s' version %d is newer than this build (%d); refusing to load",
            source,
            version,
            FILE_FORMAT_VERSION
        );
        return false;
    }
    if (!doc.contains("entities") || !doc["entities"].is_array()) {
        LOG_ERROR("Missing or invalid 'entities' array in '%s'", source);
        return false;
    }

    // Staged, so a failure leaves the live pair untouched; Merge writes the live graph.
    Scene staging;
    ResourceManager stagingResources;
    ResourceManager& assetGraph = (policy == AssetPolicy::Merge) ? resources : stagingResources;

    if (doc.contains("assets")) {
        // loadAssets guards each asset; this catches what falls outside any asset.
        try {
            AssetSerializer::loadAssets(doc["assets"], assetGraph);
        } catch (const std::exception& e) {
            LOG_ERROR("Asset load failed for '%s': %s - scene not loaded", source, e.what());
            return false;
        }
    }

    // Pass 1 creates entities and reads components; parents are linked in pass 2.
    std::vector<std::pair<uint32_t, uint32_t>> parentLinks;  // (child idx, parent idx)
    std::vector<EntityId> prefabRoots;  // instance roots to expand
    size_t entityCount = 0;
    size_t unusableIds = 0;   // tallied, not logged per entry
    size_t duplicateIds = 0;
    std::set<std::string> unknownKeys;  // one warning per key, not per entity
    const json noComponents = json::object();

    // Where the read is, for the catch below; entity 0 means outside the entity loop.
    uint32_t    entityBeingRead = 0;
    const char* blockBeingRead  = "entities";

    try {
        // Every entity exists before any component is read, so references resolve.
        // Ids are checked here: afterwards each would test as a duplicate of itself.
        std::vector<EntityId> byEntry;
        byEntry.reserve(doc["entities"].size());
        for (const auto& entry : doc["entities"]) {
            const uint32_t id = entry.value("id", 0u);
            // createEntityAt refuses a live slot; counted apart so the warning says which.
            if (staging.isAliveAtIndex(id)) {
                ++duplicateIds;
                byEntry.emplace_back();
                continue;
            }
            const EntityId entity = staging.createEntityAt(id);
            byEntry.push_back(entity);
            if (!entity) {
                ++unusableIds;
                continue;
            }
            ++entityCount;
        }

        auto bySavedSlot = [&](uint32_t slot) -> EntityId {
            if (!staging.isAliveAtIndex(slot)) {
                LOG_WARNING(
                    "'%s' names entity slot %u, which it does not hold; that reference is left empty",
                    source,
                    slot
                );
                return {};
            }
            return staging.entityAt(slot);
        };
        const EntityResolver resolve(bySavedSlot);

        for (size_t e = 0; e < doc["entities"].size(); ++e) {
            const json& entry = doc["entities"][e];
            const EntityId entity = byEntry[e];
            if (!entity) continue;
            const uint32_t id = entity.slot();
            entityBeingRead = id;

            // find(), not value(): value() deep-copies the component block.
            const auto it = entry.find("components");
            const json& components = (it != entry.end()) ? *it : noComponents;

            // Hierarchy is not loaded here; its parent index is captured below.
            loadComponents(components, staging, entity, assetGraph, resolve);
            if (components.contains("Hierarchy")) {
                const uint32_t parentIdx = CS::loadParentIndex(components["Hierarchy"]);
                if (parentIdx != std::numeric_limits<uint32_t>::max() && parentIdx != 0) {
                    parentLinks.emplace_back(id, parentIdx);
                }
            }

            if (entry.contains("prefab")) {
                PrefabInstance instance;
                instance.source = entry.value("prefab", std::string{});

                // Flatten uid -> component -> field; a non-object level is skipped.
                if (entry.contains("overrides") && entry["overrides"].is_object()) {
                    for (const auto& [uidKey, comps] : entry["overrides"].items()) {
                        if (!comps.is_object()) continue;

                        // Parsed whole: strtoul reads "head" as 0, the root.
                        uint32_t uid = 0;
                        const char* last = uidKey.data() + uidKey.size();
                        const auto [stop, ec] = std::from_chars(uidKey.data(), last, uid);
                        if (ec != std::errc{} || stop != last) {
                            LOG_WARNING(
                                "Override key '%s' in '%s' is not an entity uid; dropped",
                                uidKey.c_str(),
                                source
                            );
                            continue;
                        }

                        for (const auto& [comp, fields] : comps.items()) {
                            if (!fields.is_object()) continue;
                            for (const auto& [field, value] : fields.items()) {
                                instance.overrides.push_back(PrefabOverride{uid, comp, field, value.dump()});
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
            LOG_WARNING(
                "%zu entity record(s) in '%s' skipped: id 0 is the reserved slot and "
                    "an id above %u is not one this build will size the slot table to",
                unusableIds,
                source,
                SlotAllocator::MAX_CLAIMED_INDEX
            );
        }
        if (duplicateIds > 0) {
            LOG_WARNING(
                "%zu entity record(s) in '%s' skipped: the id was already taken by an earlier record",
                duplicateIds,
                source
            );
        }

        // After the entity pass, so the prefab's entities do not compete for saved slots.
        std::set<std::string> prefabDrift;
        for (const EntityId root : prefabRoots) {
            // Safe to hold: expansion adds no PrefabInstance (nesting is refused at save).
            const PrefabInstance& instance = staging.get<PrefabInstance>(root);
            const Prefab::BuiltSlots* slots = nullptr;
            if (instanceSlots) {
                const auto it = instanceSlots->find(root.slot());
                if (it != instanceSlots->end()) slots = &it->second;
            }
            if (!Prefab::instantiateInto(
                staging,
                assetGraph,
                instance.source,
                root,
                instance.overrides,
                &prefabDrift,
                slots
            )) {
                reportError(
                    "Scene",
                    "prefab '" + instance.source + "'",
                    "could not be opened, so the instance is empty - the reference and its "
                        "overrides are kept, so restoring the file and loading again brings the subtree back"
                );
            }
        }
        for (const std::string& message : prefabDrift) {
            LOG_WARNING("%s (kept, not applied)", message.c_str());
        }

        entityBeingRead = 0;

        // Pass 2: wire up Hierarchy::parent.
        for (const auto& [childIdx, parentIdx] : parentLinks) {
            if (!staging.isAliveAtIndex(parentIdx)) {
                LOG_WARNING(
                    "Parent slot %u not found in '%s'; entity %u left as root",
                    parentIdx,
                    source,
                    childIdx
                );
                continue;
            }
            const EntityId childId  = staging.entityAt(childIdx);
            const EntityId parentId = staging.entityAt(parentIdx);
            HierarchyOperations::setParent(staging, childId, parentId);
        }

        // Missing fields keep defaults; a mistyped one throws into the catch below.
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
            LOG_ERROR(
                "Aborted while reading entity %u of '%s': %s (live scene unchanged)",
                entityBeingRead,
                source,
                e.what()
            );
        } else {
            LOG_ERROR(
                "Aborted while reading the %s block of '%s': %s (live scene unchanged)",
                blockBeingRead,
                source,
                e.what()
            );
        }
        return false;
    }

    for (const std::string& k : unknownKeys) {
        LOG_WARNING("Unknown component key '%s' in '%s' (schema drift; dropped)", k.c_str(), source);
    }

    // compact() reclaims the sparse capacity staging grew. Merge has no graph to swap.
    scene.swap(staging);
    if (policy == AssetPolicy::Replace) resources.swap(stagingResources);
    scene.compact();

    LOG_INFO(
        "Loaded scene from '%s' (%zu entities, %zu hierarchy links)",
        source,
        entityCount,
        parentLinks.size()
    );
    return true;
}

} // namespace

bool save(const Scene& scene, const ResourceManager& resources, const std::string& path) {
    PROFILE_SCOPE("SceneSerializer::save");
    // Not const: writeJsonFile zeroes non-finite numbers.
    json doc = buildSceneJson(scene, resources);

    if (!detail::writeJsonFile(path, doc, "Scene")) return false;

    const auto& assets = doc["assets"];
    const size_t numTex = assets.contains("textures")  ? assets["textures"].size()  : 0;
    const size_t numMat = assets.contains("materials") ? assets["materials"].size() : 0;
    const size_t numMsh = assets.contains("meshes")    ? assets["meshes"].size()    : 0;
    LOG_INFO(
        "Saved scene to '%s' (%zu entities, %zu texture(s) + %zu material(s) + %zu mesh(es) referenced)",
        path.c_str(),
        doc["entities"].size(),
        numTex,
        numMat,
        numMsh
    );
    return true;
}

bool load(Scene& scene, ResourceManager& resources, const std::string& path) {
    PROFILE_SCOPE("SceneSerializer::load");
    json doc;
    if (!detail::readJsonFile(path, doc, "Scene")) return false;

    return readSceneJson(doc, scene, resources, path.c_str(), AssetPolicy::Replace, nullptr);
}

std::vector<MissingAssetRef> unresolvedRefs(
    const Scene& scene,
    const ResourceManager& resources,
    EntityId id
) {
    const MissingAssets* missing = scene.tryGet<MissingAssets>(id);
    if (!missing) return {};

    // Only asset names are read back, so entity naming does not matter.
    auto bySlot = sceneSlotName;
    const EntityNamer name(bySlot);
    json components = json::object();
    writeComponents(scene, id, components, resources, name);

    std::vector<MissingAssetRef> still;
    for (const MissingAssetRef& ref : missing->refs) {
        if (fieldLeftEmpty(components, ref)) still.push_back(ref);
    }
    return still;
}

std::string saveToString(const Scene& scene, const ResourceManager& resources) {
    PROFILE_SCOPE("SceneSerializer::saveToString");
    // As writeJsonFile does: a non-finite number would come back as a null the load refuses.
    json doc = buildSceneJson(scene, resources);
    detail::writeNonFiniteAsZero(doc, "Scene");
    return doc.dump();
}

bool loadFromString(
    const std::string& text,
    Scene& scene,
    ResourceManager& resources,
    const Prefab::InstanceSlots* instanceSlots
) {
    PROFILE_SCOPE("SceneSerializer::loadFromString");
    json doc;
    try {
        doc = json::parse(text);
    } catch (const std::exception& e) {
        LOG_ERROR("SceneSerializer::loadFromString JSON parse error: %s", e.what());
        return false;
    }

    return readSceneJson(doc, scene, resources, "<memory snapshot>", AssetPolicy::Merge, instanceSlots);
}

} // namespace Vkm::Engine::SceneSerializer
