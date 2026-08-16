#define VKM_LOG_CATEGORY "IO"

#include "io/scene/prefab.h"

#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/component/hierarchy.h"
#include "io/json_file.h"
#include "io/scene/scene_serializer.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine::Prefab {

namespace {

using nlohmann::json;

// Bumped when the layout below changes. A prefab written by a newer build is
// refused rather than half-read, the same contract scenes use.
constexpr int PREFAB_FORMAT_VERSION = 1;

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

} // namespace

bool save(const Scene& scene, EntityId root, const std::string& path,
          const ResourceManager& resources) {
    if (!scene.isAlive(root)) {
        LOG_ERROR("Prefab::save: entity is not alive");
        return false;
    }

    const std::vector<EntityId> subtree = collectSubtree(scene, root);

    // Entity ids are meaningless outside the scene that issued them, so parents
    // are stored as indices into this file's own array.
    std::unordered_map<uint32_t, size_t> indexOf;
    for (size_t i = 0; i < subtree.size(); ++i) indexOf[subtree[i].index] = i;

    json doc;
    doc["version"]  = PREFAB_FORMAT_VERSION;
    doc["entities"] = json::array();

    for (size_t i = 0; i < subtree.size(); ++i) {
        const EntityId id = subtree[i];

        json components = json::object();
        SceneSerializer::saveComponents(scene, id, components, resources);
        // The parent link is rewritten as a local index below, so drop the one
        // saveComponents wrote in scene-entity terms.
        components.erase("Hierarchy");

        json entity;
        entity["components"] = std::move(components);

        if (i > 0 && scene.has<Hierarchy>(id)) {
            const EntityId parent = scene.get<Hierarchy>(id).parent;
            auto it = indexOf.find(parent.index);
            if (it != indexOf.end()) entity["parent"] = it->second;
        }

        doc["entities"].push_back(std::move(entity));
    }

    if (!detail::writeJsonFile(path, doc, "Prefab")) return false;

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
    return root;
}

bool instantiateInto(Scene& scene, ResourceManager& resources, const std::string& path,
                     EntityId root) {
    json doc;
    if (!detail::readJsonFile(path, doc, "Prefab")) return false;

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

    const json& entities = doc["entities"];

    // Parents precede children in the file, so each link can be wired as its
    // child is created rather than in a second pass.
    std::vector<EntityId> created;
    created.reserve(entities.size());

    for (size_t i = 0; i < entities.size(); ++i) {
        const json& entry = entities[i];

        // Index 0 is the root, which the caller already owns.
        EntityId entity = (i == 0) ? root : scene.createEntity();

        if (entry.contains("components")) {
            // The root keeps the pose it was placed at; every other component,
            // and every child, comes from the prefab.
            const bool keepTransform = (i == 0) && scene.has<Transform>(entity);
            const Transform placed = keepTransform ? scene.get<Transform>(entity) : Transform{};

            SceneSerializer::loadComponents(entry["components"], scene, entity, resources);

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

} // namespace Engine::Prefab
