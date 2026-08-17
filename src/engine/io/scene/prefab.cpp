#define VKM_LOG_CATEGORY "IO"

#include "io/scene/prefab.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
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
 * the engine root, so a bare open would look beside the engine. An absolute path
 * passes through for a caller that already resolved one.
 */
std::filesystem::path resolvePath(const std::string& path) {
    return std::filesystem::path(path).is_absolute()
        ? std::filesystem::path(path)
        : (ProjectPaths::projectRoot() / path).lexically_normal();
}

/**
 * @brief Read @p path and check it is a prefab this build can build from.
 *
 * @param path Prefab reference, project-relative or absolute.
 * @param doc Receives the document on success.
 * @return True when the file parsed, its version is readable and it holds entities.
 */
bool readPrefab(const std::string& path, json& doc) {
    if (!detail::readJsonFile(resolvePath(path).string(), doc, "Prefab")) return false;

    const int version = doc.value("version", 0);
    if (version <= 0 || version > PREFAB_FORMAT_VERSION) {
        LOG_ERROR("Prefab '%s' version %d is unreadable by this build (%d)",
                  path.c_str(), version, PREFAB_FORMAT_VERSION);
        return false;
    }
    if (!doc.contains("entities") || !doc["entities"].is_array() || doc["entities"].empty()) {
        LOG_ERROR("Prefab '%s' has no entities", path.c_str());
        return false;
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
    return entities[index].value("uid", static_cast<uint32_t>(index));
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
            if (!drift) return;
            drift->insert("Override in '" + what + "' on entity " + std::to_string(uid) +
                          " (" + o.component + "." + o.field + "): " + reason);
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
        const json& was = out[o.component][o.field];
        const bool sameKind = (was.is_number() && value.is_number()) ||
                              (was.type() == value.type());
        if (!sameKind ||
            (was.is_array() && was.size() != value.size())) {
            report("type does not match the prefab's");
            continue;
        }

        out[o.component][o.field] = std::move(value);
    }

    return out;
}

} // namespace

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
    // back attached to something else.
    uint32_t nextUid = 0;
    for (EntityId id : subtree) {
        if (scene.has<PrefabEntity>(id)) {
            nextUid = std::max(nextUid, scene.get<PrefabEntity>(id).uid + 1);
        }
    }

    // The root's uid is fixed, so an override on it needs no lookup.
    if (!scene.has<PrefabEntity>(root)) scene.add(root, PrefabEntity{});
    scene.get<PrefabEntity>(root).uid = PrefabEntity::ROOT;
    nextUid = std::max(nextUid, uint32_t{1});

    for (size_t i = 0; i < subtree.size(); ++i) {
        const EntityId id = subtree[i];

        if (!scene.has<PrefabEntity>(id)) scene.add(id, PrefabEntity{nextUid++});

        json components = json::object();
        SceneSerializer::saveComponents(scene, id, components, resources);
        // The parent link is rewritten as a local index below, so drop the one
        // saveComponents wrote in scene-entity terms.
        components.erase("Hierarchy");

        json entity;
        entity["uid"]        = scene.get<PrefabEntity>(id).uid;
        entity["components"] = std::move(components);

        if (i > 0 && scene.has<Hierarchy>(id)) {
            const EntityId parent = scene.get<Hierarchy>(id).parent;
            auto it = indexOf.find(parent.index);
            if (it != indexOf.end()) entity["parent"] = it->second;
        }

        doc["entities"].push_back(std::move(entity));
    }

    doc["nextUid"] = nextUid;

    if (!detail::writeJsonFile(path, doc, "Prefab")) return false;

    // The subtree that was just written becomes an instance of what it wrote.
    // Without this the master copy is a loose subtree: a scene save would store
    // its entities inline, a scene load would rebuild them with no uids, and the
    // next save of this prefab would renumber the file and detach every override
    // in one go.
    if (!scene.has<PrefabInstance>(root)) scene.add(root, PrefabInstance{});
    scene.get<PrefabInstance>(root).source = path;

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
        scene.destroyEntity(root);
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

    for (size_t i = 0; i < entities.size(); ++i) {
        const json& entry = entities[i];

        // Index 0 is the root, which the caller already owns.
        EntityId entity = (i == 0) ? root : scene.createEntity();

        // The identity an override addresses.
        const uint32_t uid = uidAt(entities, i);
        if (!scene.has<PrefabEntity>(entity)) scene.add(entity, PrefabEntity{});
        scene.get<PrefabEntity>(entity).uid = uid;

        if (entry.contains("components")) {
            // The root keeps the pose it was placed at; every other component,
            // and every child, comes from the prefab.
            const bool keepTransform = (i == 0) && scene.has<Transform>(entity);
            const Transform placed = keepTransform ? scene.get<Transform>(entity) : Transform{};

            const json patched = overrides.empty()
                ? entry["components"]
                : applyOverrides(entry["components"], uid, overrides, path, drift);
            SceneSerializer::loadComponents(patched, scene, entity, resources);

            if (keepTransform) scene.get<Transform>(entity) = placed;
        }
        if (!scene.has<Transform>(entity)) scene.add(entity, Transform{});

        if (i > 0 && entry.contains("parent")) {
            const size_t parentIndex = entry.value("parent", size_t{0});
            if (parentIndex < created.size()) {
                HierarchyOperations::setParent(scene, entity, created[parentIndex]);
            }
        }
        created.push_back(entity);
    }

    return true;
}

bool reloadComponent(Scene& scene, ResourceManager& resources, const std::string& path,
                     EntityId entity, uint32_t uid, const std::string& component,
                     const std::vector<PrefabOverride>& overrides) {
    if (!scene.isAlive(entity)) return false;

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
        SceneSerializer::loadComponents(one, scene, entity, resources);
        return true;
    }
    return false;
}

} // namespace Engine::Prefab
