#include "framework/prefab_overrides.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "ecs/scene.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/prefab_entity.h"
#include "io/scene/prefab.h"
#include "system/hierarchy/hierarchy_operations.h"

#include "framework/editor_commands.h"
#include "framework/editor_state.h"

namespace Engine::PrefabOverrides {

namespace {

using nlohmann::json;

// The entries addressing one entity's component, in stored order.
std::vector<PrefabOverride> entriesFor(const std::vector<PrefabOverride>& list,
                                       uint32_t uid, const std::string& component) {
    std::vector<PrefabOverride> out;
    for (const PrefabOverride& o : list) {
        if (o.uid == uid && o.component == component) out.push_back(o);
    }
    return out;
}

// Make @p entries the whole of what @p list says about (uid, component).
void replaceEntries(std::vector<PrefabOverride>& list, uint32_t uid, const std::string& component,
                    const std::vector<PrefabOverride>& entries) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const PrefabOverride& o) {
        return o.uid == uid && o.component == component;
    }), list.end());
    list.insert(list.end(), entries.begin(), entries.end());
}

// Set one field's entry, keeping the order the others were written in so the
// inspector's list does not reshuffle while values are edited.
void setEntry(std::vector<PrefabOverride>& entries, uint32_t uid, const char* component,
              const std::string& field, std::string value) {
    for (PrefabOverride& o : entries) {
        if (o.field != field) continue;
        o.value = std::move(value);
        return;
    }
    entries.push_back(PrefabOverride{uid, component, field, std::move(value)});
}

} // namespace

EntityId instanceRoot(const Scene& scene, EntityId id) {
    if (!scene.isAlive(id)) return {};
    if (scene.has<PrefabInstance>(id)) return id;

    EntityId cursor = scene.has<Hierarchy>(id) ? scene.get<Hierarchy>(id).parent : EntityId{};
    for (int depth = 0; depth < 32 && scene.isAlive(cursor); ++depth) {
        if (scene.has<PrefabInstance>(cursor)) return cursor;
        if (!scene.has<Hierarchy>(cursor)) break;
        cursor = scene.get<Hierarchy>(cursor).parent;
    }
    return {};
}

std::vector<std::string> overriddenFields(const Scene& scene, EntityId id,
                                          const char* component) {
    std::vector<std::string> fields;

    const EntityId root = instanceRoot(scene, id);
    if (!root || !scene.has<PrefabEntity>(id)) return fields;

    const uint32_t uid = scene.get<PrefabEntity>(id).uid;
    for (const PrefabOverride& o : scene.get<PrefabInstance>(root).overrides) {
        if (o.uid == uid && o.component == component) fields.push_back(o.field);
    }
    return fields;
}

void apply(Scene& scene, ResourceManager& resources, EntityId root, EntityId target,
           const std::string& component, const std::vector<PrefabOverride>& entries) {
    if (!scene.isAlive(root) || !scene.has<PrefabInstance>(root)) return;
    if (!scene.isAlive(target) || !scene.has<PrefabEntity>(target)) return;

    const uint32_t uid = scene.get<PrefabEntity>(target).uid;
    PrefabInstance& instance = scene.get<PrefabInstance>(root);
    replaceEntries(instance.overrides, uid, component, entries);

    Prefab::reloadComponent(scene, resources, instance.source, target, uid, component,
                            instance.overrides);
    if (component == "Transform") HierarchyOperations::markDirty(scene, target);
}

std::unique_ptr<Command> recordFields(Scene& scene, ResourceManager& resources, EntityId id,
                                      const char* component, const json& before, const json& after,
                                      const char* label) {
    const EntityId root = instanceRoot(scene, id);
    if (!root || !scene.has<PrefabEntity>(id)) return nullptr;

    const uint32_t uid = scene.get<PrefabEntity>(id).uid;

    // The root's Transform is the instance's own pose - the scene stores it
    // beside the reference and puts it back after the prefab is built, so an
    // override on it could never take effect.
    if (uid == PrefabEntity::ROOT && std::strcmp(component, "Transform") == 0) return nullptr;

    std::vector<PrefabOverride>& list = scene.get<PrefabInstance>(root).overrides;
    const std::vector<PrefabOverride> restore = entriesFor(list, uid, component);

    std::vector<PrefabOverride> entries = restore;
    bool changed = false;
    for (auto field = after.begin(); field != after.end(); ++field) {
        const auto was = before.find(field.key());
        if (was != before.end() && *was == field.value()) continue;
        setEntry(entries, uid, component, field.key(), field.value().dump());
        changed = true;
    }
    if (!changed) return nullptr;

    // The live component already holds what the user typed, so the list is all
    // there is to update - re-reading it from the prefab here would put a file
    // read in the middle of every frame of a drag.
    replaceEntries(list, uid, component, entries);

    return std::make_unique<PrefabOverrideCommand>(
        resources, root, id, component, restore, std::move(entries), label);
}

void revert(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id,
            const char* component, const std::string& field) {
    const EntityId root = instanceRoot(scene, id);
    if (!root || !scene.has<PrefabEntity>(id)) return;

    const uint32_t uid = scene.get<PrefabEntity>(id).uid;
    const std::vector<PrefabOverride> restore =
        entriesFor(scene.get<PrefabInstance>(root).overrides, uid, component);

    std::vector<PrefabOverride> entries = restore;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const PrefabOverride& o) {
        return o.field == field;
    }), entries.end());
    if (entries.size() == restore.size()) return;

    apply(scene, resources, root, id, component, entries);
    state.commands.push(std::make_unique<PrefabOverrideCommand>(
        resources, root, id, component, restore, std::move(entries), "Revert Override"));
    state.markSceneDirty();
}

} // namespace Engine::PrefabOverrides
