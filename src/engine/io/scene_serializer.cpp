#include "io/scene_serializer.h"

#include <fstream>
#include <limits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "ecs/entity.h"
#include "io/asset_serializer.h"
#include "io/component_serializer.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"   // EnvironmentConfig
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine::SceneSerializer {

namespace {

using nlohmann::json;
namespace CS = ComponentSerializer;

constexpr int FILE_FORMAT_VERSION = 1;

/// Try to read a component from an entity and emit a JSON entry for it.
template<typename Component, typename Fn>
void emitIfPresent(const Scene& scene, EntityId id,
                   json& components, const char* key, Fn&& saver)
{
    if (scene.has<Component>(id)) {
        components[key] = saver(scene.get<Component>(id));
    }
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

        emitIfPresent<Name>     (scene, id, components, "Name",      [&](const Name& c)      { return CS::save(c); });
        emitIfPresent<Transform>(scene, id, components, "Transform", [&](const Transform& c) { return CS::save(c); });
        emitIfPresent<Camera>   (scene, id, components, "Camera",    [&](const Camera& c)    { return CS::save(c); });
        emitIfPresent<Light>    (scene, id, components, "Light",     [&](const Light& c)     { return CS::save(c); });
        emitIfPresent<Mesh>     (scene, id, components, "Mesh",      [&](const Mesh& c)      { return CS::save(c, resources); });
        emitIfPresent<Hierarchy>(scene, id, components, "Hierarchy", [&](const Hierarchy& c) { return CS::save(c); });
        emitIfPresent<Animation>(scene, id, components, "Animation", [&](const Animation& c) { return CS::save(c); });
        emitIfPresent<EnvironmentConfig>(scene, id, components, "Environment", [&](const EnvironmentConfig& c) { return CS::save(c); });
        // WorldTransform is derived — not persisted.

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
    if (version != FILE_FORMAT_VERSION) {
        LOG_ERROR("SceneSerializer::load: file version %d, expected %d", version, FILE_FORMAT_VERSION);
        return false;
    }
    if (!doc.contains("entities") || !doc["entities"].is_array()) {
        LOG_ERROR("SceneSerializer::load: missing or invalid 'entities' array");
        return false;
    }

    // Populate the asset graph before touching entities — Mesh components
    // will resolve their handles by name during entity load. Idempotent:
    // assets already present in ResourceManager are kept as-is.
    if (doc.contains("assets")) {
        AssetSerializer::loadAssets(doc["assets"], resources);
    }

    scene.clear();

    // Pass 1: create each entity at its saved slot index and populate
    // non-relational components. Hierarchy::parent is captured for pass 2
    // because the parent might not have been created yet on first sight.
    std::vector<std::pair<uint32_t, uint32_t>> parentLinks;  // (child idx, parent idx)
    size_t entityCount = 0;

    for (const auto& entry : doc["entities"]) {
        const uint32_t id = entry.value("id", 0u);
        if (id == 0) {
            LOG_WARNING("SceneSerializer::load: entity with id=0 skipped (slot 0 reserved)");
            continue;
        }
        const Entity entity = scene.createEntityAt(id);
        ++entityCount;

        const auto& components = entry.value("components", json::object());

        if (components.contains("Name")) {
            Name c; CS::load(components["Name"], c);
            scene.add(entity, std::move(c));
        }
        if (components.contains("Transform")) {
            Transform c; CS::load(components["Transform"], c);
            scene.add(entity, std::move(c));
        }
        if (components.contains("Camera")) {
            Camera c; CS::load(components["Camera"], c);
            scene.add(entity, std::move(c));
        }
        if (components.contains("Light")) {
            Light c; CS::load(components["Light"], c);
            scene.add(entity, std::move(c));
        }
        if (components.contains("Mesh")) {
            Mesh c; CS::load(components["Mesh"], c, resources);
            scene.add(entity, std::move(c));
        }
        if (components.contains("Animation")) {
            Animation a; CS::load(components["Animation"], a);
            scene.add(entity, std::move(a));
        }
        if (components.contains("Environment")) {
            EnvironmentConfig c; CS::load(components["Environment"], c);
            scene.add(entity, std::move(c));
        }
        if (components.contains("Hierarchy")) {
            const uint32_t parentIdx = CS::loadParentIndex(components["Hierarchy"]);
            if (parentIdx != std::numeric_limits<uint32_t>::max() && parentIdx != 0) {
                parentLinks.emplace_back(id, parentIdx);
            }
        }
    }

    // Pass 2: wire up Hierarchy::parent now that every entity exists at its
    // saved slot. setParent rebuilds firstChild/nextSibling/prevSibling on
    // both sides and marks WorldTransform dirty.
    for (const auto& [childIdx, parentIdx] : parentLinks) {
        if (!scene.isAliveAtIndex(parentIdx)) {
            LOG_WARNING("SceneSerializer::load: parent slot %u not found in file; entity %u left as root",
                parentIdx, childIdx);
            continue;
        }
        const EntityId childId {childIdx,  scene.generationOf(childIdx)};
        const EntityId parentId{parentIdx, scene.generationOf(parentIdx)};
        HierarchyOperations::setParent(scene, childId, parentId);
    }

    // SparseSet capacity may have grown then shrunk during clear()/load —
    // reclaim wasted slots so the loaded scene doesn't carry orphan capacity.
    scene.compact();

    LOG_INFO("Loaded scene from '%s' (%zu entities, %zu hierarchy links)",
        path.c_str(), entityCount, parentLinks.size());
    return true;
}

} // namespace Engine::SceneSerializer
