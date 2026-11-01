#define VKM_LOG_CATEGORY "IO"

#include "io/scene/prefab.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "ecs/scene.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/prefab_instances.h"
#include "io/scene/scene_assets.h"
#include "io/project/roots.h"
#include "io/json/json_file.h"
#include "io/json/json_number.h"
#include "io/scene/scene_components.h"
#include "io/scene/scene_serializer.h"

namespace Vkm::Engine::Prefab {

namespace {

using nlohmann::json;

// Bumped when the layout below changes. A newer prefab is refused rather than
// half-read, as scenes are.
constexpr int PREFAB_FORMAT_VERSION = 4;

/**
 * @brief The unsigned number @p from stores at @p key, or @p fallback.
 *
 * A key of the wrong type reads as absent, where json::value would throw.
 *
 * @param from Object to read; a non-object holds no keys.
 * @param key Key to look for.
 * @param fallback Result when the key is absent or not an unsigned number.
 * @return The stored number, or @p fallback.
 */
uint32_t numberOr(const json& from, const char* key, uint32_t fallback) {
    const auto it   = from.find(key);
    const bool held = it != from.end() && it->is_number_unsigned() && detail::fitsIn<uint32_t>(*it);
    return held ? it->get<uint32_t>() : fallback;
}

/**
 * @brief The identity of the entity stored at @p index.
 *
 * Index 0 is the root by contract; a file that disagrees is repaired.
 *
 * @param entities The prefab document's entities array, accepted by readPrefab.
 * @param index    Position of the entry in it.
 * @return The entry's uid; PrefabEntity::ROOT at index 0.
 */
uint32_t uidAt(const json& entities, size_t index) {
    if (index == 0) return PrefabEntity::ROOT;
    return entities[index]["uid"].get<uint32_t>();
}

/**
 * @brief Whether @p doc, read from @p path, is a prefab this build can build from.
 *
 * Past here, each entry and its component block are objects, every entry past
 * the root carries a uid, uids are unique, and a child's parent is an earlier
 * entry; a file that disagrees is refused whole.
 */
bool acceptsPrefab(const std::string& path, const json& doc) {
    if (!detail::readsFormatVersion(doc, PREFAB_FORMAT_VERSION, "Prefab", path.c_str())) return false;
    if (!doc.contains("entities") || !doc["entities"].is_array() || doc["entities"].empty()) {
        LOG_ERROR("Prefab '%s' has no entities", path.c_str());
        return false;
    }
    if (const auto assets = doc.find("assets"); assets != doc.end() && !assets->is_object()) {
        LOG_ERROR("Prefab '%s' has an assets block that is not an object", path.c_str());
        return false;
    }

    const json& entities = doc.at("entities");
    std::set<uint32_t> seen;
    for (size_t i = 0; i < entities.size(); ++i) {
        const auto components = entities[i].find("components");
        if (!entities[i].is_object() || (components != entities[i].end() && !components->is_object())) {
            LOG_ERROR("Prefab '%s' entry %zu does not describe an entity", path.c_str(), i);
            return false;
        }
        const auto stored = entities[i].find("uid");
        const bool named  = stored != entities[i].end() && stored->is_number_unsigned()
            && detail::fitsIn<uint32_t>(*stored);
        if (i > 0 && !named) {
            LOG_ERROR("Prefab '%s' entry %zu carries no uid", path.c_str(), i);
            return false;
        }
        // An override addresses a uid: with a duplicate, applying it patches both
        // while re-reading stops at the first.
        const uint32_t uid = uidAt(entities, i);
        if (!seen.insert(uid).second) {
            LOG_ERROR(
                "Prefab '%s' gives uid %u to two entities; an override addresses one",
                path.c_str(),
                uid
            );
            return false;
        }

        // The build parents each entry to the one its parent names, unchecked, so the
        // parent must be an earlier entry: a name from 1 to i.
        if (i > 0) {
            const auto parent = entities[i].find("parent");
            if (parent == entities[i].end() || !parent->is_number_unsigned()
                || parent->get<uint64_t>() == 0 || parent->get<uint64_t>() > i) {
                LOG_ERROR(
                    "Prefab '%s' entry %zu does not name an earlier entry as its parent",
                    path.c_str(),
                    i
                );
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief A prefab file as readPrefab last accepted it, while its write time and size hold.
 */
struct ReadPrefab {
    std::filesystem::file_time_type written;
    std::uintmax_t                  bytes = 0;
    std::shared_ptr<const json>     doc;
};

// By resolved path. Prefabs are built on the main thread alone, as scenes load there.
std::unordered_map<std::string, ReadPrefab> g_read;

/**
 * @brief The prefab at @p path, parsed once until the file changes, if this build can build from it.
 *
 * What a build would leave unread of it is warned of as it is parsed.
 *
 * @param path Prefab reference, project-relative or absolute.
 * @return The document, or null when it does not read or is refused (acceptsPrefab).
 */
std::shared_ptr<const json> readPrefab(const std::string& path) {
    const std::string resolved = Roots::resolveProjectPath(path).string();
    std::error_code ec;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(resolved, ec);
    const std::uintmax_t bytes = ec ? 0 : std::filesystem::file_size(resolved, ec);
    if (const auto held = g_read.find(resolved); held != g_read.end()) {
        if (!ec && held->second.written == written && held->second.bytes == bytes) return held->second.doc;
        g_read.erase(held);
    }

    json doc;
    if (!detail::readJsonFile(resolved, doc, "Prefab") || !acceptsPrefab(path, doc)) return nullptr;

    // Said once a read of the file, not once an instance.
    SceneSerializer::UnreadKeys unread;
    for (const json& entry : doc.at("entities")) {
        const auto components = entry.find("components");
        if (components != entry.end()) SceneSerializer::collectUnread(*components, unread);
    }
    SceneSerializer::warnUnread(unread, path.c_str());

    auto accepted = std::make_shared<const json>(std::move(doc));
    if (!ec) g_read[resolved] = ReadPrefab{written, bytes, accepted};
    return accepted;
}

/**
 * @brief Bring the assets @p doc names into @p resources before it is read.
 *
 * A name resolves to nothing unless its asset is loaded. A block that cannot be
 * loaded is reported and does not stop the build.
 *
 * @param doc Prefab document, already accepted by readPrefab.
 * @param path Prefab reference, for the message.
 * @param resources Receives the assets.
 */
void ensureAssets(const json& doc, const std::string& path, ResourceManager& resources) {
    const auto assets = doc.find("assets");
    if (assets == doc.end()) return;

    try {
        SceneAssets::loadAssets(*assets, resources);
    } catch (const std::exception& e) {
        LOG_ERROR("Prefab '%s': assets could not be loaded: %s", path.c_str(), e.what());
    }
}

/**
 * @brief One drift line: the prefab, the override's address, and why it was not applied.
 *
 * @param what   Prefab path.
 * @param o      The override that was not applied.
 * @param reason Why not.
 * @return The message.
 */
std::string driftMessage(const std::string& what, const PrefabOverride& o, const char* reason) {
    return "Override in '" + what + "' on entity " + std::to_string(o.uid)
        + " (" + o.component + "." + o.field + "): " + reason;
}

/**
 * @brief Does @p value still fit the shape the prefab holds in @p was?
 *
 * Walks both, since an array holding the wrong elements passes a one-level check
 * and then throws in the component loader. Numbers are interchangeable (an
 * int-valued float writes as an int); objects need exactly the same keys. Only a
 * numeric array (vec3, quat) is length-checked; an array of objects is a list.
 *
 * @param was The prefab's value for the field.
 * @param value The override's value.
 * @return True when the override can be handed to the loader.
 */
bool sameShape(const json& was, const json& value) {
    if (was.is_number()) return value.is_number();
    if (was.type() != value.type()) return false;

    if (was.is_array()) {
        if (was.empty()) return true;  // nothing to take an element schema from
        if (was.front().is_number() && was.size() != value.size()) return false;
        for (const json& element : value) {
            if (!sameShape(was.front(), element)) return false;
        }
        return true;
    }
    if (was.is_object()) {
        if (was.size() != value.size()) return false;
        for (const auto& [key, sub] : was.items()) {
            const auto it = value.find(key);
            if (it == value.end() || !sameShape(sub, *it)) return false;
        }
        return true;
    }
    return true;
}

/**
 * @brief Write @p overrides for @p uid into a copy of that entity's components.
 *
 * Merged as JSON before loadComponents: a loader replaces what it reads (a vector
 * field, ScriptComponent's behavior list), so a patch read over a loaded component
 * would discard it. An override that no longer fits is reported, not applied.
 *
 * @param base The entity's components object from the prefab document.
 * @param uid The entity being built.
 * @param overrides Every override on this instance; only this uid's are read.
 * @param what Prefab path, for the drift messages.
 * @param drift Receives one message per override that could not be applied.
 * @return The components object to load, patched where the override fitted.
 */
json applyOverrides(
    const json& base,
    uint32_t uid,
    const std::vector<PrefabOverride>& overrides,
    const std::string& what,
    std::set<std::string>* drift
) {
    json out = base;

    for (const PrefabOverride& o : overrides) {
        if (o.uid != uid) continue;

        const auto report = [&](const char* reason) {
            if (drift) drift->insert(driftMessage(what, o, reason));
        };

        // The root's Transform is the instance's pose, reapplied after load, so an
        // override on it could never take effect.
        if (uid == PrefabEntity::ROOT && o.component == sceneKey<Transform>()) {
            report("the root Transform is the instance's own pose");
            continue;
        }
        // A Script override would work, and must not: the component is one field
        // holding the whole behavior list, so it would replace every behavior.
        if (o.component == sceneKey<ScriptComponent>()) {
            report("behavior fields are the prefab's, not per-instance");
            continue;
        }
        if (!out.contains(o.component)) {
            report("no such component");
            continue;
        }
        if (!out[o.component].contains(o.field)) {
            report("no such field");
            continue;
        }

        json value;
        try {
            value = json::parse(o.value);
        } catch (const std::exception&) {
            report("value is not valid JSON");
            continue;
        }

        // Without this a drifted field throws in the loader and the whole
        // instance is abandoned.
        if (!sameShape(out[o.component][o.field], value)) {
            report("type does not match the prefab's");
            continue;
        }

        out[o.component][o.field] = std::move(value);
    }

    return out;
}

} // namespace

bool save(Scene& scene, EntityId root, const std::string& path, const ResourceManager& resources) {
    if (!scene.isAlive(root)) {
        LOG_ERROR("Prefab::save: entity is not alive");
        return false;
    }

    const std::vector<EntityId> subtree = HierarchyOperations::collectSubtree(scene, root);

    // An instance below the root would be flattened into this file, severed from
    // its prefab.
    for (size_t i = 1; i < subtree.size(); ++i) {
        if (scene.has<PrefabInstance>(subtree[i])) {
            LOG_ERROR(
                "Prefab::save: '%s' contains a prefab instance; nested prefabs are not supported",
                path.c_str()
            );
            return false;
        }
    }

    // A root inside another instance is not this file's to define.
    if (isInsideInstance(scene, root)) {
        LOG_ERROR(
            "Prefab::save: '%s' is part of a prefab instance; nested prefabs are not supported",
            path.c_str()
        );
        return false;
    }

    // Entity ids mean nothing outside their scene, so the file names entities by position.
    std::unordered_map<uint32_t, size_t> indexOf;
    for (size_t i = 0; i < subtree.size(); ++i) indexOf[subtree[i].slot()] = i;

    // An entity is named by its file index plus one, so zero stays "none"; a
    // reference outside the subtree is dropped.
    auto byLocalIndex = [&](EntityId e) -> uint32_t {
        const auto it = indexOf.find(e.slot());
        return it != indexOf.end() ? static_cast<uint32_t>(it->second) + 1 : 0u;
    };
    const EntityNamer name(byLocalIndex);

    json doc;
    doc["version"]  = PREFAB_FORMAT_VERSION;
    doc["entities"] = json::array();

    // Seeded from the file being overwritten too: the entity with the highest uid
    // may have just been deleted, and only the file remembers it.
    uint32_t nextUid = 0;
    {
        json existing;
        std::error_code ec;
        const std::filesystem::path resolved = Roots::resolveProjectPath(path);
        if (std::filesystem::exists(resolved, ec) && detail::readJsonFile(resolved, existing, "Prefab")) {
            nextUid = numberOr(existing, "nextUid", 0);
        }
    }
    for (EntityId id : subtree) {
        if (const PrefabEntity* stamped = scene.tryGet<PrefabEntity>(id)) {
            nextUid = std::max(nextUid, stamped->uid + 1);
        }
    }

    // Stamped onto the scene only once the file is written: a subtree numbered
    // against no file would hand one number out twice.
    nextUid = std::max(nextUid, uint32_t{1});
    std::set<uint32_t> taken{PrefabEntity::ROOT};
    std::vector<uint32_t> uids(subtree.size(), PrefabEntity::ROOT);
    for (size_t i = 1; i < subtree.size(); ++i) {
        const PrefabEntity* stamped = scene.tryGet<PrefabEntity>(subtree[i]);
        const bool keep = stamped
            && stamped->uid != PrefabEntity::ROOT
            && taken.insert(stamped->uid).second;
        uids[i] = keep ? stamped->uid : nextUid++;
    }

    for (size_t i = 0; i < subtree.size(); ++i) {
        const EntityId id = subtree[i];

        json components = json::object();
        SceneSerializer::saveComponents(scene, id, components, resources, name);
        // Hierarchy names its parent by slot; the entry names it below, by position.
        components.erase(sceneKey<Hierarchy>());

        json entity;
        entity["uid"]        = uids[i];
        entity["components"] = std::move(components);

        // The root's parent is outside the file; every other entry's is in it.
        if (i > 0) entity["parent"] = name(scene.get<Hierarchy>(id).parent);

        doc["entities"].push_back(std::move(entity));
    }

    doc["nextUid"] = nextUid;
    doc["assets"]  = SceneAssets::saveAssetsForEntities(scene, subtree, resources);

    const std::filesystem::path written = Roots::resolveProjectPath(path);
    // Dropped whatever the write does: a file system's coarse write time could hide the change.
    g_read.erase(written.string());
    if (!detail::writeJsonFile(written, doc, "Prefab")) return false;

    for (size_t i = 0; i < subtree.size(); ++i) {
        PrefabEntity* stamped = scene.tryGet<PrefabEntity>(subtree[i]);
        if (!stamped) stamped = &scene.add(subtree[i], PrefabEntity{});
        stamped->uid = uids[i];
    }

    PrefabInstance* held = scene.tryGet<PrefabInstance>(root);
    if (!held) held = &scene.add(root, PrefabInstance{});
    PrefabInstance& instance = *held;
    instance.source = path;

    instance.overrides.clear();

    LOG_INFO("Saved prefab '%s' (%zu entities)", path.c_str(), subtree.size());
    return true;
}

EntityId instantiate(Scene& scene, ResourceManager& resources, const std::string& path, const Transform& at) {
    const EntityId root = instantiate(scene, resources, path);
    if (!scene.isAlive(root)) return {};

    // The loader adds a Transform when the prefab did not save one.
    scene.get<Transform>(root) = at;
    return root;
}

EntityId instantiate(Scene& scene, ResourceManager& resources, const std::string& path) {
    EntityId root = scene.createEntity();
    if (!instantiateInto(scene, resources, path, root)) {
        // The subtree: a partial build has already parented entities under it.
        HierarchyOperations::destroyHierarchy(scene, root);
        return {};
    }

    scene.add(root, PrefabInstance{}).source = path;
    return root;
}

bool instantiateInto(
    Scene& scene,
    ResourceManager& resources,
    const std::string& path,
    EntityId root,
    const std::vector<PrefabOverride>& overrides,
    std::set<std::string>* drift,
    const BuiltSlots* slots
) {
    const std::shared_ptr<const json> doc = readPrefab(path);
    if (!doc) return false;
    ensureAssets(*doc, path, resources);

    const json& entities = doc->at("entities");

    std::vector<EntityId> created;
    created.reserve(entities.size());
    std::set<uint32_t> built;

    // Destroys every entity this call created; the root (index 0) is the caller's.
    const auto abandon = [&]() {
        for (size_t i = created.size(); i-- > 1;) scene.destroyEntity(created[i]);
    };

    // An earlier build's slot for this uid while it is free, so what addressed
    // that entity by slot still finds it.
    const auto createFor = [&](uint32_t uid) {
        if (slots) {
            const auto it = slots->find(uid);
            if (it != slots->end() && !scene.isAliveAtIndex(it->second)) {
                return scene.createEntityAt(it->second);
            }
        }
        return scene.createEntity();
    };

    // Every entity first: the file names them by position, so a reference
    // resolves only once they all exist.
    for (size_t i = 0; i < entities.size(); ++i) {
        created.push_back(i == 0 ? root : createFor(uidAt(entities, i)));
    }
    auto byLocalIndex = [&](uint32_t local) -> EntityId {
        return local <= created.size() ? created[local - 1] : EntityId{};
    };
    const EntityResolver resolve(byLocalIndex);

    for (size_t i = 0; i < entities.size(); ++i) {
        const json& entry = entities[i];
        EntityId entity   = created[i];

        // The identity an override addresses.
        const uint32_t uid    = uidAt(entities, i);
        PrefabEntity* stamped = scene.tryGet<PrefabEntity>(entity);
        if (!stamped) stamped = &scene.add(entity, PrefabEntity{});
        stamped->uid = uid;
        built.insert(uid);

        if (entry.contains("components")) {
            // The root keeps the pose it was placed at.
            const Transform* at = i == 0 ? scene.tryGet<Transform>(entity) : nullptr;
            const bool keepTransform = at != nullptr;
            const Transform placed = at ? *at : Transform{};

            const json& authored = entry.at("components");
            json        patched;
            if (!overrides.empty()) patched = applyOverrides(authored, uid, overrides, path, drift);

            // A malformed block throws; it costs this instance, not the caller.
            try {
                const json& components = overrides.empty() ? authored : patched;
                SceneSerializer::loadComponents(components, scene, entity, resources, resolve);
            } catch (const std::exception& e) {
                LOG_ERROR("Prefab '%s' entity %u could not be built: %s", path.c_str(), uid, e.what());
                abandon();
                return false;
            }

            // Re-fetched: adding a Transform in loadComponents may have moved
            // the storage the pointer above pointed into.
            if (keepTransform) scene.get<Transform>(entity) = placed;
        }
        if (!scene.has<Transform>(entity)) scene.add(entity, Transform{});

        if (i > 0) {
            // Total: readPrefab refused any entry not naming an earlier parent.
            HierarchyOperations::setParent(scene, entity, resolve(numberOr(entry, "parent", 0u)));
        }
    }

    // An override naming an entity the file lacks: applyOverrides never sees it.
    if (drift) {
        for (const PrefabOverride& o : overrides) {
            if (built.count(o.uid) == 0) {
                drift->insert(driftMessage(path, o, "no such entity in the prefab"));
            }
        }
    }

    return true;
}

bool reloadComponent(
    Scene& scene,
    ResourceManager& resources,
    const std::string& path,
    EntityId entity,
    uint32_t uid,
    const std::string& component,
    const std::vector<PrefabOverride>& overrides
) {
    if (!scene.isAlive(entity)) return false;

    // The root's Transform is the instance's pose; re-reading the prefab's would
    // teleport the instance when an override is dropped.
    if (uid == PrefabEntity::ROOT && component == sceneKey<Transform>()) return false;

    const std::shared_ptr<const json> doc = readPrefab(path);
    if (!doc) return false;
    ensureAssets(*doc, path, resources);

    const json& entities = doc->at("entities");
    for (size_t i = 0; i < entities.size(); ++i) {
        if (uidAt(entities, i) != uid) continue;
        if (!entities[i].contains("components")) return false;

        const json patched = applyOverrides(entities[i].at("components"), uid, overrides, path, nullptr);
        if (!patched.contains(component)) return false;

        // A cross-entity reference resolves within this entity's instance, not to
        // whatever scene entity holds that file position.
        const EntityId instanceRoot = instanceRootOf(scene, entity);
        const std::vector<EntityId> subtree =
            instanceRoot ? HierarchyOperations::collectSubtree(scene, instanceRoot) : std::vector<EntityId>{};
        auto byFileUid = [&](uint32_t local) -> EntityId {
            if (local > entities.size()) return {};
            const uint32_t wanted = uidAt(entities, local - 1);
            for (EntityId id : subtree) {
                const PrefabEntity* held = scene.tryGet<PrefabEntity>(id);
                if (held && held->uid == wanted) return id;
            }
            return {};
        };
        const EntityResolver resolve(byFileUid);

        // One key, so loadComponents runs this component's loader and no other.
        json one = json::object();
        one[component] = patched[component];
        try {
            SceneSerializer::loadComponents(one, scene, entity, resources, resolve);
        } catch (const std::exception& e) {
            LOG_ERROR(
                "Prefab '%s' entity %u: %s could not be re-read: %s",
                path.c_str(),
                uid,
                component.c_str(),
                e.what()
            );
            return false;
        }

        return true;
    }
    return false;
}

bool definesComponent(const std::string& path, uint32_t uid, const std::string& component) {
    const std::shared_ptr<const json> doc = readPrefab(path);
    if (!doc) return false;

    const json& entities = doc->at("entities");
    for (size_t i = 0; i < entities.size(); ++i) {
        if (uidAt(entities, i) != uid) continue;
        const auto it = entities[i].find("components");
        return it != entities[i].end() && it->contains(component);
    }
    return false;
}

} // namespace Vkm::Engine::Prefab
