#define VKM_LOG_CATEGORY "IO"

#include "io/scene/scene_serializer.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "base/container/slot_allocator.h"
#include "base/reflect/reflect.h"
#include "debug/error/engine_error_log.h"
#include "io/json/json_number.h"
#include "base/profile/profiler.h"
#include "ecs/scene.h"
#include "ecs/entity.h"
#include "ecs/component/scene/missing_assets.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/scene_assets.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_components.h"
#include "io/json/json_file.h"
#include "io/json/reflect_json.h"
#include "resource/resource_manager.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"

namespace Vkm::Engine::SceneSerializer {

namespace {

// Load recreates each entity at its saved slot.
uint32_t sceneSlotName(EntityId e) { return e.slot(); }

using nlohmann::json;
namespace CS = ComponentSerializer;

// Bumped when the document layout changes. A file written by a newer build is refused.
constexpr int FILE_FORMAT_VERSION = 3;

/**
 * @brief Move what a component's load could not resolve onto @p e, emptying @p missed.
 *
 * Only here are entity and component known, the address a save needs.
 *
 * @param s Scene holding the entity.
 * @param e Entity the component was read into.
 * @param key Scene-format key the component was stored under.
 * @param missed What its load could not resolve.
 */
void recordUnresolved(Scene& s, EntityId e, const char* key, std::vector<CS::UnresolvedRef>& missed) {
    if (missed.empty()) return;

    MissingAssets* record = s.tryGet<MissingAssets>(e);
    if (!record) record = &s.add(e, MissingAssets{});
    for (CS::UnresolvedRef& ref : missed) {
        record->refs.push_back({key, std::move(ref.field), std::move(ref.name), ref.type});
    }
    missed.clear();
}

/**
 * @brief Read one component from @p src, when @p key is present, into @p e.
 *
 * Assigns over an existing component: a prefab instance root is read twice
 * (prefab file, then scene), and SparseSet::add would silently keep the first.
 *
 * @tparam T Component type to read.
 * @tparam Args Extra arguments this component's loader takes (a ResourceManager and the
 *              list its unresolved names go in for an R row, an EntityResolver for an E row).
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
    try {
        CS::load(*it, component, std::forward<Args>(args)...);
    } catch (const std::exception& error) {
        // nlohmann says nothing of where; the key is added here, the entity one level up.
        throw std::runtime_error(std::string("component '") + key + "': " + error.what());
    }
    if (T* existing = s.tryGet<T>(e)) *existing = std::move(component);
    else                               s.add(e, std::move(component));
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
    if (const Hierarchy* h = s.tryGet<Hierarchy>(id)) c[sceneKey<Hierarchy>()] = CS::save(*h);
}

#undef VKM_SCENE_SAVE
#undef VKM_SCENE_SAVE_R
#undef VKM_SCENE_SAVE_E

/**
 * @brief @p ref's field, when it came out of writeComponents as an empty string.
 *
 * Only then does a kept name go back; a slot filled since keeps its value, and a
 * list grown shorter has no element left to take it.
 *
 * @param c An entity's components, as writeComponents wrote them.
 * @param ref A reference the load could not resolve.
 * @return The field, or null when it is absent, not a string, or not empty.
 */
json* fieldLeftEmpty(json& c, const MissingAssetRef& ref) {
    const auto component = c.find(ref.component);
    if (component == c.end()) return nullptr;

    json* at     = &*component;
    size_t start = 0;
    while (at) {
        const size_t dot = ref.field.find('.', start);
        const std::string step = ref.field.substr(start, dot - start);
        if (at->is_object()) {
            const auto it = at->find(step);
            at = it == at->end() ? nullptr : &*it;
        } else if (at->is_array()) {
            size_t index = 0;
            const auto [end, error] = std::from_chars(step.data(), step.data() + step.size(), index);
            const bool whole = error == std::errc{} && end == step.data() + step.size();
            at = (whole && index < at->size()) ? &(*at)[index] : nullptr;
        } else {
            at = nullptr;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return (at && at->is_string() && at->get_ref<const std::string&>().empty()) ? at : nullptr;
}

} // namespace

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r, const EntityNamer& name) {
    writeComponents(s, id, c, r, name);

    // Unresolved names go back over the "" written above (docs/reference/io.md).
    const MissingAssets* missing = s.tryGet<MissingAssets>(id);
    if (!missing) return;
    for (const MissingAssetRef& ref : missing->refs) {
        if (json* field = fieldLeftEmpty(c, ref)) *field = ref.name;
    }
}

void saveComponents(const Scene& s, EntityId id, json& c, const ResourceManager& r) {
    // Only while the entity lives: a deleted one's slot goes to the next entity made, which would
    // take over the reference when the file is read.
    auto bySlot = [&s](EntityId e) { return s.isAlive(e) ? sceneSlotName(e) : 0u; };
    const EntityNamer name(bySlot);
    saveComponents(s, id, c, r, name);
}

#define VKM_SCENE_LOAD(Type, Key)   loadInto<Type>(src, Key, s, e);
#define VKM_SCENE_LOAD_E(Type, Key) loadInto<Type>(src, Key, s, e, resolve);
#define VKM_SCENE_LOAD_R(Type, Key)            \
    loadInto<Type>(src, Key, s, e, r, missed); \
    recordUnresolved(s, e, Key, missed);

void loadComponents(
    const json& src,
    Scene& s,
    EntityId e,
    const ResourceManager& r,
    const EntityResolver& resolve
) {
    // A load that throws ends this one, and its names with it.
    std::vector<CS::UnresolvedRef> missed;
    VKM_SCENE_COMPONENTS(VKM_SCENE_LOAD, VKM_SCENE_LOAD_R, VKM_SCENE_LOAD_E)
}

#undef VKM_SCENE_LOAD
#undef VKM_SCENE_LOAD_R
#undef VKM_SCENE_LOAD_E

namespace {

/**
 * @brief Read @p key whole as a prefab uid; strtoul would read "head" as 0, the root.
 */
bool uidOf(const std::string& key, uint32_t& uid) {
    const char* last = key.data() + key.size();
    const auto [stop, ec] = std::from_chars(key.data(), last, uid);
    return ec == std::errc{} && stop == last;
}

/**
 * @brief Where an instance's record says each entity of its subtree stood: uid to slot.
 *
 * An entry naming no uid, the root, or a slot no entity can hold is skipped and counted in
 * @p unusable, and its entity is built above every slot the file places.
 */
Prefab::BuiltSlots slotsOf(const json& entry, size_t& unusable) {
    Prefab::BuiltSlots slots;
    const auto it = entry.find("slots");
    if (it == entry.end()) return slots;
    if (!it->is_object()) {
        ++unusable;
        return slots;
    }
    for (const auto& [key, slot] : it->items()) {
        uint32_t uid = 0;
        const uint64_t at = slot.is_number_unsigned() ? slot.get<uint64_t>() : 0;
        if (!uidOf(key, uid) || uid == PrefabEntity::ROOT || at == 0 || at > SlotAllocator::MAX_CLAIMED_INDEX) {
            ++unusable;
            continue;
        }
        slots[uid] = static_cast<uint32_t>(at);
    }
    return slots;
}

/**
 * @brief Give each empty list in @p value, at any depth, one default element, so a save of it
 *        writes every key a saved one can hold.
 */
template<typename T>
void growLists(T& value) {
    if constexpr (ReflectJson::IsVector<T>::value) {
        if (value.empty()) value.emplace_back();
        growLists(value.front());
    } else if constexpr (Reflect::IS_REFLECTED<T>) {
        Reflect::forEachField(value, [](std::string_view, auto& field) { growLists(field); });
    }
}

/// A default @p T, its lists grown (growLists).
template<typename T>
T shapeOf() {
    T value{};
    growLists(value);
    return value;
}

/**
 * @brief What a save writes for each component, by its key, Hierarchy's included: the keys a load
 *        reads.
 */
const std::map<std::string, json>& componentShapes() {
    static const std::map<std::string, json> shapes = [] {
        // Defaults name no asset and no entity, so neither is asked.
        const ResourceManager noAssets;
        auto noEntity = [](EntityId) { return 0u; };
        const EntityNamer name(noEntity);

        std::map<std::string, json> out;
#define VKM_SCENE_SHAPE(Type, Key)   out[Key] = CS::save(shapeOf<Type>());
#define VKM_SCENE_SHAPE_R(Type, Key) out[Key] = CS::save(shapeOf<Type>(), noAssets);
#define VKM_SCENE_SHAPE_E(Type, Key) out[Key] = CS::save(shapeOf<Type>(), name);
        VKM_SCENE_COMPONENTS(VKM_SCENE_SHAPE, VKM_SCENE_SHAPE_R, VKM_SCENE_SHAPE_E)
#undef VKM_SCENE_SHAPE
#undef VKM_SCENE_SHAPE_R
#undef VKM_SCENE_SHAPE_E
        out[sceneKey<Hierarchy>()] = CS::save(Hierarchy{});
        return out;
    }();
    return shapes;
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
    doc["assets"]   = SceneAssets::saveAssetsForScene(scene, resources);
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
            json transform = std::move(components[sceneKey<Transform>()]);
            json hierarchy = std::move(components[sceneKey<Hierarchy>()]);
            components = json::object();
            if (!transform.is_null()) components[sceneKey<Transform>()] = std::move(transform);
            if (!hierarchy.is_null()) components[sceneKey<Hierarchy>()] = std::move(hierarchy);
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

            // Where each entity of the subtree stands, by uid: a load builds it back there, so a
            // reference into the instance names it.
            json slots = json::object();
            for (const auto& [uid, slot] : Prefab::builtSlotsOf(scene, id)) slots[std::to_string(uid)] = slot;
            if (!slots.empty()) entity["slots"] = std::move(slots);
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
 * @param generations The source world's slot generations, or null to number
 *        every slot from generation 0.
 * @return true on success; false (logged) leaves the scene untouched, and the
 *         asset graph too under AssetPolicy::Replace.
 */
bool readSceneJson(
    const json& doc,
    Scene& scene,
    ResourceManager& resources,
    const char* source,
    AssetPolicy policy,
    const std::vector<uint32_t>* generations
) {
    if (!detail::readsFormatVersion(doc, FILE_FORMAT_VERSION, "Scene", source)) return false;
    if (!doc.contains("entities") || !doc["entities"].is_array()) {
        LOG_ERROR("Missing or invalid 'entities' array in '%s'", source);
        return false;
    }

    // Staged, so a failure leaves the live pair untouched; Merge writes the live graph.
    Scene staging;
    ResourceManager stagingResources;
    ResourceManager& assetGraph = (policy == AssetPolicy::Merge) ? resources : stagingResources;
    if (generations) staging.startSlotsAt(*generations);

    if (doc.contains("assets")) {
        // loadAssets guards each asset; this catches what falls outside any asset.
        try {
            SceneAssets::loadAssets(doc["assets"], assetGraph);
        } catch (const std::exception& e) {
            LOG_ERROR("Asset load failed for '%s': %s - scene not loaded", source, e.what());
            return false;
        }
    }

    // Pass 1 creates entities, builds instances and reads components; parents are linked in pass 2.
    std::vector<std::pair<uint32_t, uint32_t>> parentLinks;  // (child idx, parent idx)
    size_t entityCount  = 0;
    size_t unusableIds  = 0;   // tallied, not logged per entry
    size_t duplicateIds = 0;
    UnreadKeys unread;  // one warning per key, not per entity
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
            const uint32_t id = detail::numberAt(entry, "id", 0u);
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

        // Instances are built before any component is read, so a reference into one resolves. Each
        // entity goes back to the slot the file says it stood in; one the file does not place, as an
        // entity the prefab gained since, goes above them all, taking no slot another waits for.
        std::vector<std::pair<EntityId, Prefab::BuiltSlots>> instances;
        uint32_t placedPast    = 0;
        size_t   unplacedSlots = 0;  // tallied, said once below
        for (size_t e = 0; e < doc["entities"].size(); ++e) {
            const json& entry     = doc["entities"][e];
            const EntityId entity = byEntry[e];
            if (!entity || !entry.contains("prefab")) continue;
            entityBeingRead = entity.slot();

            PrefabInstance instance;
            instance.source = entry.value("prefab", std::string{});

            // Flatten uid -> component -> field; a non-object level is skipped.
            if (entry.contains("overrides") && entry["overrides"].is_object()) {
                for (const auto& [uidKey, comps] : entry["overrides"].items()) {
                    if (!comps.is_object()) continue;

                    uint32_t uid = 0;
                    if (!uidOf(uidKey, uid)) {
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

            Prefab::BuiltSlots slots = slotsOf(entry, unplacedSlots);
            for (const auto& placed : slots) placedPast = std::max(placedPast, placed.second + 1);
            staging.add(entity, std::move(instance));
            instances.emplace_back(entity, std::move(slots));
        }

        staging.keepSlotsBelow(placedPast);
        std::set<std::string> prefabDrift;
        for (const auto& [root, slots] : instances) {
            entityBeingRead = root.slot();
            std::set<uint32_t> named;  // two uids naming one slot: the second cannot have it
            for (const auto& placed : slots) {
                unplacedSlots += staging.isAliveAtIndex(placed.second) || !named.insert(placed.second).second ? 1 : 0;
            }
            // Safe to hold: expansion adds no PrefabInstance (nesting is refused at save).
            const PrefabInstance& instance = staging.get<PrefabInstance>(root);
            if (!Prefab::instantiateInto(
                staging,
                assetGraph,
                instance.source,
                root,
                instance.overrides,
                &prefabDrift,
                &slots
            )) {
                // Its entities stand empty where the file placed them, so what names one still does
                // and the next save writes their slots back.
                for (const auto& [uid, slot] : slots) {
                    if (staging.isAliveAtIndex(slot)) continue;
                    const EntityId held = staging.createEntityAt(slot);
                    staging.add(held, Transform{});
                    staging.add(held, PrefabEntity{uid});
                    HierarchyOperations::setParent(staging, held, root);
                }
                reportError(
                    "Scene",
                    "prefab '" + instance.source + "'",
                    "could not be opened, so the instance's entities stand empty - the reference, its "
                        "overrides and slots are kept, so restoring the file and loading again brings the subtree back"
                );
            }
        }
        staging.keepSlotsBelow(0);
        for (const std::string& message : prefabDrift) {
            LOG_WARNING("%s (kept, not applied)", message.c_str());
        }
        if (unplacedSlots > 0) {
            LOG_WARNING(
                "%zu prefab instance slot(s) in '%s' name no slot or one another entity holds; those "
                    "entities were built elsewhere, or not at all where the prefab would not open, so what "
                    "named them by slot may name another",
                unplacedSlots,
                source
            );
        }

        // An instance root's Transform is read over the one its prefab gave it.
        for (size_t e = 0; e < doc["entities"].size(); ++e) {
            const json& entry     = doc["entities"][e];
            const EntityId entity = byEntry[e];
            if (!entity) continue;
            const uint32_t id = entity.slot();
            entityBeingRead = id;

            // find(), not value(): value() deep-copies the component block.
            const auto it = entry.find("components");
            const json& components = (it != entry.end()) ? *it : noComponents;

            // Hierarchy is not loaded here; its parent index is captured below.
            loadComponents(components, staging, entity, assetGraph, resolve);
            const auto hierarchy = components.find(sceneKey<Hierarchy>());
            if (hierarchy != components.end()) {
                const uint32_t parentIdx = CS::loadParentIndex(*hierarchy);
                if (parentIdx != std::numeric_limits<uint32_t>::max() && parentIdx != 0) {
                    parentLinks.emplace_back(id, parentIdx);
                }
            }

            collectUnread(components, unread);
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
            ReflectJson::unreadKeys(*it, CS::save(shapeOf<Environment>()), blockBeingRead, unread.fields);
        }
        blockBeingRead = "physics";
        if (auto it = doc.find("physics"); it != doc.end() && it->is_object()) {
            ComponentSerializer::load(*it, staging.physics());
            ReflectJson::unreadKeys(*it, CS::save(shapeOf<PhysicsSettings>()), blockBeingRead, unread.fields);
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

    warnUnread(unread, source);

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

void collectUnread(const json& components, UnreadKeys& unread) {
    if (!components.is_object()) return;
    const std::map<std::string, json>& shapes = componentShapes();
    for (const auto& [key, block] : components.items()) {
        const auto shape = shapes.find(key);
        if (shape == shapes.end()) unread.components.insert(key);
        else                       ReflectJson::unreadKeys(block, shape->second, key, unread.fields);
    }
    // A behavior's fields are its type's, which the Script row's shape cannot name.
    const auto script = components.find(sceneKey<ScriptComponent>());
    if (script != components.end()) CS::unreadBehaviorFields(*script, unread.fields);
}

void warnUnread(const UnreadKeys& unread, const char* source) {
    for (const std::string& key : unread.components) {
        LOG_WARNING("Unknown component key '%s' in '%s' (schema drift; dropped)", key.c_str(), source);
    }
    for (const std::string& field : unread.fields) {
        LOG_WARNING("Unknown field '%s' in '%s' (schema drift; dropped)", field.c_str(), source);
    }
}

bool save(const Scene& scene, const ResourceManager& resources, const std::string& path) {
    PROFILE_SCOPE("SceneSerializer::save");
    // Not const: writeJsonFile zeroes non-finite numbers.
    json doc = buildSceneJson(scene, resources);

    if (!detail::writeJsonFile(path, doc, "Scene")) return false;

    LOG_INFO(
        "Saved scene to '%s' (%zu entities, %zu asset(s) referenced)",
        path.c_str(),
        doc["entities"].size(),
        SceneAssets::namesIn(doc["assets"]).size()
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
    detail::tidyNumbers(doc, "Scene");
    return doc.dump();
}

bool loadFromString(
    const std::string& text,
    Scene& scene,
    ResourceManager& resources,
    const std::vector<uint32_t>* generations
) {
    PROFILE_SCOPE("SceneSerializer::loadFromString");
    json doc;
    try {
        doc = json::parse(text);
    } catch (const std::exception& e) {
        LOG_ERROR("SceneSerializer::loadFromString JSON parse error: %s", e.what());
        return false;
    }

    return readSceneJson(doc, scene, resources, "<memory snapshot>", AssetPolicy::Merge, generations);
}

} // namespace Vkm::Engine::SceneSerializer
