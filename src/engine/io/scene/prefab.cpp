#define VKM_LOG_CATEGORY "IO"

#include "io/scene/prefab.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/prefab_entity.h"
#include "ecs/component/prefab_instance.h"
#include "io/project_paths.h"
#include "io/json_file.h"
#include "io/scene/scene_serializer.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine::Prefab {

namespace {

using nlohmann::json;

// Bumped when the layout below changes. A prefab written by a newer build is
// refused rather than half-read, the same contract scenes use.
constexpr int PREFAB_FORMAT_VERSION = 2;

/**
 * @brief Where a prefab reference points on disk.
 *
 * A prefab path in a scene names project content, and the working directory is
 * the engine root, so a bare open or write would land beside the engine. An
 * absolute path passes through for a caller that already resolved one.
 */
std::filesystem::path resolvePath(const std::string& path) {
    return std::filesystem::path(path).is_absolute()
        ? std::filesystem::path(path)
        : (ProjectPaths::projectRoot() / path).lexically_normal();
}

/**
 * @brief The unsigned number @p from stores at @p key, or @p fallback.
 *
 * A prefab is authored JSON, so every key in it may be missing, or hold a value
 * of any type. json::value converts what it finds, and converting a string to a
 * number throws out of whoever asked - the editor, on a file its own picker
 * offered it. A key of the wrong type reads here as an absent one, which is what
 * the rest of the reader already does with a key it does not find.
 *
 * @param from Object to read; a value that is not an object holds no keys.
 * @param key Key to look for.
 * @param fallback Result when the key is absent or is not an unsigned number.
 * @return The stored number, or @p fallback.
 */
uint32_t numberOr(const json& from, const char* key, uint32_t fallback) {
    const auto it = from.find(key);
    return (it != from.end() && it->is_number_unsigned()) ? it->get<uint32_t>() : fallback;
}

/**
 * @brief Read @p path and check it is a prefab this build can build from.
 *
 * The one place a prefab document is read, so it is the one place its shape is
 * established: past here an entry is an object and its component block, if it
 * has one, is an object too. A file that disagrees is refused whole rather than
 * half-built, because it was hand-edited into something that describes nothing.
 *
 * @param path Prefab reference, project-relative or absolute.
 * @param doc Receives the document on success.
 * @return True when the file parsed, its version is readable and it holds entities.
 */
bool readPrefab(const std::string& path, json& doc) {
    if (!detail::readJsonFile(resolvePath(path).string(), doc, "Prefab")) return false;

    const uint32_t version = numberOr(doc, "version", 0);
    if (version == 0 || version > PREFAB_FORMAT_VERSION) {
        LOG_ERROR("Prefab '%s' version %u is unreadable by this build (%d)",
                  path.c_str(), version, PREFAB_FORMAT_VERSION);
        return false;
    }
    if (!doc.contains("entities") || !doc["entities"].is_array() || doc["entities"].empty()) {
        LOG_ERROR("Prefab '%s' has no entities", path.c_str());
        return false;
    }

    const json& entities = doc["entities"];
    for (size_t i = 0; i < entities.size(); ++i) {
        const auto components = entities[i].find("components");
        if (!entities[i].is_object() ||
            (components != entities[i].end() && !components->is_object())) {
            LOG_ERROR("Prefab '%s' entry %zu does not describe an entity", path.c_str(), i);
            return false;
        }
    }
    return true;
}

/**
 * @brief The identity of the entity stored at @p index.
 *
 * Index 0 is the root by contract, so a file that disagrees is repaired rather
 * than trusted.
 */
uint32_t uidAt(const json& entities, size_t index) {
    if (index == 0) return PrefabEntity::ROOT;
    return numberOr(entities[index], "uid", static_cast<uint32_t>(index));
}

/**
 * @brief Collect @p root and every descendant, parents before children.
 *
 * Ordering matters on load: a child's parent must already exist when the link
 * is wired, and a breadth-first walk guarantees it without a second pass.
 */
std::vector<EntityId> collectSubtree(const Scene& scene, EntityId root) {
    std::vector<EntityId> out{root};
    for (size_t i = 0; i < out.size(); ++i) {
        HierarchyOperations::forEachChild(scene, out[i], [&](EntityId child) {
            if (scene.isAlive(child)) out.push_back(child);
        });
    }
    return out;
}

/**
 * @brief One drift line, in the shape the table in prefab.h describes.
 */
std::string driftMessage(const std::string& what, const PrefabOverride& o, const char* reason) {
    return "Override in '" + what + "' on entity " + std::to_string(o.uid) +
           " (" + o.component + "." + o.field + "): " + reason;
}

/**
 * @brief Does @p value still fit the shape the prefab holds in @p was?
 *
 * The prefab's own value is the schema, so the comparison walks both together
 * rather than stopping at the top-level type. An array of the right kind holding
 * the wrong elements passes a one-level check and then throws inside the
 * component loader, which is the failure this exists to prevent.
 *
 * Numbers are interchangeable (an int-valued float writes as an int), an object
 * must carry exactly the same keys, and an array must hold elements of the same
 * shape. Only a numeric array is length-checked - that is a fixed-width vector
 * (vec3, quat) whose length is part of its type, where an array of objects is a
 * list its loader reads at whatever length it finds.
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
 * The merge is JSON-on-JSON and happens *before* loadComponents, which matters:
 * the loaders construct a fresh component and assign, and several are not
 * safe to run twice (Mesh's load clobbers its handles from an absent key,
 * ScriptComponent's clears its behaviors, LOD's appends without clearing). Patch
 * the document and let each loader run exactly once, as it already does.
 *
 * An override that no longer fits the prefab is kept in memory and reported
 * rather than applied - see the drift table in prefab.h.
 *
 * @param base The entity's components object from the prefab document.
 * @param uid The entity being built.
 * @param overrides Every override on this instance; only this uid's are read.
 * @param what Prefab path, for the drift messages.
 * @param drift Receives one message per override that could not be applied.
 * @return The components object to load, patched where the override fitted.
 */
json applyOverrides(const json& base, uint32_t uid,
                    const std::vector<PrefabOverride>& overrides,
                    const std::string& what, std::set<std::string>* drift) {
    json out = base;

    for (const PrefabOverride& o : overrides) {
        if (o.uid != uid) continue;

        const auto report = [&](const char* reason) {
            if (drift) drift->insert(driftMessage(what, o, reason));
        };

        // The root's Transform is the instance's pose, written by the scene and
        // reapplied after the components load, so an override on it could never
        // take effect. Say so rather than appear to work.
        if (uid == PrefabEntity::ROOT && o.component == "Transform") {
            report("the root Transform is the instance's own pose");
            continue;
        }
        if (!out.contains(o.component))            { report("no such component"); continue; }
        if (!out[o.component].contains(o.field))   { report("no such field");     continue; }

        json value;
        try {
            value = json::parse(o.value);
        } catch (const std::exception&) {
            report("value is not valid JSON");
            continue;
        }

        // Type-check against what the prefab holds. Without this a drifted field
        // throws inside the component loader, and that aborts the whole scene
        // load - one stale override would take down every scene using it.
        if (!sameShape(out[o.component][o.field], value)) {
            report("type does not match the prefab's");
            continue;
        }

        out[o.component][o.field] = std::move(value);
    }

    return out;
}

} // namespace

bool isInsideInstance(const Scene& scene, EntityId id) {
    if (!scene.has<Hierarchy>(id)) return false;

    // Bounded by the live entity count rather than by a depth: a chain longer
    // than that has already revisited an entity, so a hand-edited file that made
    // a cycle still terminates, and no real subtree is cut short. A walk that
    // stopped early would answer "not inside an instance" for an entity that is,
    // and the scene serializer writes what this says.
    EntityId cursor = scene.get<Hierarchy>(id).parent;
    for (size_t step = 0; step <= scene.entityCount() && scene.isAlive(cursor); ++step) {
        if (scene.has<PrefabInstance>(cursor)) return true;
        if (!scene.has<Hierarchy>(cursor)) break;
        cursor = scene.get<Hierarchy>(cursor).parent;
    }
    return false;
}

bool save(Scene& scene, EntityId root, const std::string& path,
          const ResourceManager& resources) {
    if (!scene.isAlive(root)) {
        LOG_ERROR("Prefab::save: entity is not alive");
        return false;
    }

    const std::vector<EntityId> subtree = collectSubtree(scene, root);

    // Refuse a subtree that already contains an instance below the root. Saving
    // it would flatten that instance into this file, silently severing it from
    // the prefab it came from - the opposite of what nesting looks like it does.
    for (size_t i = 1; i < subtree.size(); ++i) {
        if (scene.has<PrefabInstance>(subtree[i])) {
            LOG_ERROR("Prefab::save: '%s' contains a prefab instance; nested prefabs "
                      "are not supported", path.c_str());
            return false;
        }
    }

    // And the same refusal from the other direction: a root inside somebody
    // else's instance is not this file's to define. Writing it would renumber
    // that entity's uid and stamp an instance marker inside an instance, which
    // is the nesting refused above, arrived at by re-parenting.
    if (isInsideInstance(scene, root)) {
        LOG_ERROR("Prefab::save: '%s' is part of a prefab instance; nested prefabs "
                  "are not supported", path.c_str());
        return false;
    }

    // Entity ids are meaningless outside the scene that issued them, so parents
    // are stored as indices into this file's own array.
    std::unordered_map<uint32_t, size_t> indexOf;
    for (size_t i = 0; i < subtree.size(); ++i) indexOf[subtree[i].index] = i;

    json doc;
    doc["version"]  = PREFAB_FORMAT_VERSION;
    doc["entities"] = json::array();

    // Uids are handed out in walk order on the first save and kept on every
    // later one, so an override written against this prefab still resolves after
    // it is re-saved with entities added, removed or reordered. nextUid is the
    // high-water mark: never reused, so a deleted entity's number cannot come
    // back attached to something else. It has to be seeded from the file being
    // overwritten - the entity that held the highest number may be the one that
    // was just deleted, and the live subtree no longer remembers it.
    uint32_t nextUid = 0;
    {
        json existing;
        std::error_code ec;
        const std::filesystem::path resolved = resolvePath(path);
        if (std::filesystem::exists(resolved, ec) &&
            detail::readJsonFile(resolved, existing, "Prefab")) {
            nextUid = numberOr(existing, "nextUid", 0);
        }
    }
    for (EntityId id : subtree) {
        if (scene.has<PrefabEntity>(id)) {
            nextUid = std::max(nextUid, scene.get<PrefabEntity>(id).uid + 1);
        }
    }

    // Decided here and stamped onto the scene only once the file is on disk, so
    // a save that could not be written leaves the subtree as it found it. A
    // subtree left carrying numbers no file answers to would hand one of them
    // out twice the next time a prefab was written over it. The root's is fixed,
    // so an override on it needs no lookup.
    nextUid = std::max(nextUid, uint32_t{1});
    std::vector<uint32_t> uids(subtree.size(), PrefabEntity::ROOT);
    for (size_t i = 1; i < subtree.size(); ++i) {
        uids[i] = scene.has<PrefabEntity>(subtree[i]) ? scene.get<PrefabEntity>(subtree[i]).uid
                                                      : nextUid++;
    }

    for (size_t i = 0; i < subtree.size(); ++i) {
        const EntityId id = subtree[i];

        json components = json::object();
        SceneSerializer::saveComponents(scene, id, components, resources);
        // The parent link is rewritten as a local index below, so drop the one
        // saveComponents wrote in scene-entity terms.
        components.erase("Hierarchy");

        json entity;
        entity["uid"]        = uids[i];
        entity["components"] = std::move(components);

        if (i > 0 && scene.has<Hierarchy>(id)) {
            const EntityId parent = scene.get<Hierarchy>(id).parent;
            auto it = indexOf.find(parent.index);
            if (it != indexOf.end()) entity["parent"] = it->second;
        }

        doc["entities"].push_back(std::move(entity));
    }

    doc["nextUid"] = nextUid;

    if (!detail::writeJsonFile(resolvePath(path), doc, "Prefab")) return false;

    for (size_t i = 0; i < subtree.size(); ++i) {
        if (!scene.has<PrefabEntity>(subtree[i])) scene.add(subtree[i], PrefabEntity{});
        scene.get<PrefabEntity>(subtree[i]).uid = uids[i];
    }

    // The subtree that was just written becomes an instance of what it wrote.
    // Without this the master copy is a loose subtree: a scene save would store
    // its entities inline, a scene load would rebuild them with no uids, and the
    // next save of this prefab would renumber the file and detach every override
    // in one go.
    if (!scene.has<PrefabInstance>(root)) scene.add(root, PrefabInstance{});
    PrefabInstance& instance = scene.get<PrefabInstance>(root);
    instance.source = path;

    // The file just written holds what the overrides said, so keeping them would
    // pin this instance to those values forever: every later edit of the prefab
    // would reach every instance except the one it was authored from.
    instance.overrides.clear();

    LOG_INFO("Saved prefab '%s' (%zu entities)", path.c_str(), subtree.size());
    return true;
}

EntityId instantiate(Scene& scene, ResourceManager& resources, const std::string& path,
                     const Transform& at) {
    const EntityId root = instantiate(scene, resources, path);
    if (!scene.isAlive(root)) return {};

    // Replace the authored pose. The root always carries a Transform: the
    // loader adds one when the prefab did not save it.
    scene.get<Transform>(root) = at;
    HierarchyOperations::markDirty(scene, root);
    return root;
}

EntityId instantiate(Scene& scene, ResourceManager& resources, const std::string& path) {
    EntityId root = scene.createEntity();
    if (!instantiateInto(scene, resources, path, root)) {
        // The subtree, not the root: a build that stopped partway has already
        // parented whatever it managed to create under it.
        HierarchyOperations::destroyHierarchy(scene, root);
        return {};
    }

    // What makes the result an instance rather than a loose copy of the prefab's
    // entities: without it a scene save writes the whole subtree inline and the
    // link to the file is gone. A caller that brings its own root marks it
    // itself - the scene loader has to, because the overrides it read belong on
    // the component before the subtree is built from it.
    scene.add(root, PrefabInstance{});
    scene.get<PrefabInstance>(root).source = path;
    return root;
}

bool instantiateInto(Scene& scene, ResourceManager& resources, const std::string& path,
                     EntityId root, const std::vector<PrefabOverride>& overrides,
                     std::set<std::string>* drift) {
    json doc;
    if (!readPrefab(path, doc)) return false;

    const json& entities = doc["entities"];

    // Parents precede children in the file, so each link can be wired as its
    // child is created rather than in a second pass.
    std::vector<EntityId> created;
    created.reserve(entities.size());
    std::set<uint32_t> built;

    // A failure leaves nothing of this call behind. The entity being built is
    // not parented yet, so destroying the root's subtree - which is all a caller
    // that owns the root can do - cannot reach it; it would stay loose in the
    // scene, outside every instance, and the next save would write it out as an
    // entity of its own. The root itself belongs to the caller.
    const auto abandon = [&](EntityId building) {
        if (building && building != root) scene.destroyEntity(building);
        for (size_t i = created.size(); i-- > 1;) scene.destroyEntity(created[i]);
    };

    for (size_t i = 0; i < entities.size(); ++i) {
        const json& entry = entities[i];

        // Index 0 is the root, which the caller already owns.
        EntityId entity = (i == 0) ? root : scene.createEntity();

        // The identity an override addresses.
        const uint32_t uid = uidAt(entities, i);
        if (!scene.has<PrefabEntity>(entity)) scene.add(entity, PrefabEntity{});
        scene.get<PrefabEntity>(entity).uid = uid;
        built.insert(uid);

        if (entry.contains("components")) {
            // The root keeps the pose it was placed at; every other component,
            // and every child, comes from the prefab.
            const bool keepTransform = (i == 0) && scene.has<Transform>(entity);
            const Transform placed = keepTransform ? scene.get<Transform>(entity) : Transform{};

            const json patched = overrides.empty()
                ? entry["components"]
                : applyOverrides(entry["components"], uid, overrides, path, drift);

            // The loaders throw on a malformed block, and this is the boundary
            // that has to stop it: above are a scene load that would abort
            // whole, and an editor that would go down on a hand-edited file.
            try {
                SceneSerializer::loadComponents(patched, scene, entity, resources);
            } catch (const std::exception& e) {
                LOG_ERROR("Prefab '%s' entity %u could not be built: %s",
                          path.c_str(), uid, e.what());
                abandon(entity);
                return false;
            }

            if (keepTransform) scene.get<Transform>(entity) = placed;
        }
        if (!scene.has<Transform>(entity)) scene.add(entity, Transform{});

        if (i > 0 && entry.contains("parent")) {
            // Parents precede children, so an index the walk has not reached
            // names nothing - and that is where a value that is not an index
            // lands, leaving the entity a root of its own rather than under the
            // instance root by accident.
            const size_t parentIndex = numberOr(entry, "parent", entities.size());
            if (parentIndex < created.size()) {
                HierarchyOperations::setParent(scene, entity, created[parentIndex]);
            }
        }
        created.push_back(entity);
    }

    // The one drift case applyOverrides cannot see: it is only handed the
    // entities the file has, so an override naming one it does not would pass
    // through every entity unmentioned.
    if (drift) {
        for (const PrefabOverride& o : overrides) {
            if (built.count(o.uid) == 0) {
                drift->insert(driftMessage(path, o, "no such entity in the prefab"));
            }
        }
    }

    return true;
}

bool reloadComponent(Scene& scene, ResourceManager& resources, const std::string& path,
                     EntityId entity, uint32_t uid, const std::string& component,
                     const std::vector<PrefabOverride>& overrides) {
    if (!scene.isAlive(entity)) return false;

    // The root's Transform is the instance's own pose, and the prefab's authored
    // one is not a value to give it back - re-reading it here would teleport the
    // instance the moment one of its overrides was dropped.
    if (uid == PrefabEntity::ROOT && component == "Transform") return false;

    json doc;
    if (!readPrefab(path, doc)) return false;

    const json& entities = doc["entities"];
    for (size_t i = 0; i < entities.size(); ++i) {
        if (uidAt(entities, i) != uid) continue;
        if (!entities[i].contains("components")) return false;

        const json patched = applyOverrides(entities[i]["components"], uid, overrides, path, nullptr);
        if (!patched.contains(component)) return false;

        // One key, so loadComponents runs this component's loader and no other.
        json one = json::object();
        one[component] = patched[component];
        try {
            SceneSerializer::loadComponents(one, scene, entity, resources);
        } catch (const std::exception& e) {
            LOG_ERROR("Prefab '%s' entity %u: %s could not be re-read: %s",
                      path.c_str(), uid, component.c_str(), e.what());
            return false;
        }
        return true;
    }
    return false;
}

bool definesComponent(const std::string& path, uint32_t uid, const std::string& component) {
    json doc;
    if (!readPrefab(path, doc)) return false;

    const json& entities = doc["entities"];
    for (size_t i = 0; i < entities.size(); ++i) {
        if (uidAt(entities, i) != uid) continue;
        const auto it = entities[i].find("components");
        return it != entities[i].end() && it->contains(component);
    }
    return false;
}

} // namespace Engine::Prefab
