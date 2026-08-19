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

// The entity carrying @p uid inside the instance rooted at @p root. Breadth
// first, because an instance is a handful of entities and the walk runs once per
// undo step rather than per frame.
EntityId entityWithUid(const Scene& scene, EntityId root, uint32_t uid) {
    if (!scene.isAlive(root)) return {};

    std::vector<EntityId> pending{root};
    for (size_t i = 0; i < pending.size(); ++i) {
        if (scene.has<PrefabEntity>(pending[i]) &&
            scene.get<PrefabEntity>(pending[i]).uid == uid) {
            return pending[i];
        }
        HierarchyOperations::forEachChild(scene, pending[i], [&](EntityId child) {
            if (scene.isAlive(child)) pending.push_back(child);
        });
    }
    return {};
}

} // namespace

EntityId instanceRoot(const Scene& scene, EntityId id) {
    if (!scene.isAlive(id)) return {};
    if (scene.has<PrefabInstance>(id)) return id;

    // The walk Prefab::isInsideInstance makes, bounded the same way, because the
    // two have to give the same answer: an edit recorded against an entity the
    // saver leaves out of the file is an edit that disappears on the next load.
    EntityId cursor = scene.has<Hierarchy>(id) ? scene.get<Hierarchy>(id).parent : EntityId{};
    for (size_t step = 0; step <= scene.entityCount() && scene.isAlive(cursor); ++step) {
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

    // The root's Transform is the instance's own pose, so an entry against it is
    // drift the prefab never applies - see the table in prefab.h. Offering it
    // would mark a field as the instance's when it always was, beside a control
    // that gives it back to nothing.
    if (uid == PrefabEntity::ROOT && std::strcmp(component, "Transform") == 0) return fields;

    for (const PrefabOverride& o : scene.get<PrefabInstance>(root).overrides) {
        if (o.uid == uid && o.component == component) fields.push_back(o.field);
    }
    return fields;
}

bool apply(Scene& scene, ResourceManager& resources, EntityId root, uint32_t uid,
           const std::string& component, const std::vector<PrefabOverride>& entries) {
    if (!scene.isAlive(root) || !scene.has<PrefabInstance>(root)) return false;

    const EntityId target = entityWithUid(scene, root, uid);
    if (!target) return false;

    PrefabInstance& instance = scene.get<PrefabInstance>(root);
    replaceEntries(instance.overrides, uid, component, entries);

    const bool reread = Prefab::reloadComponent(scene, resources, instance.source, target, uid,
                                                component, instance.overrides);
    if (component == "Transform") HierarchyOperations::markDirty(scene, target);
    return reread;
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

    PrefabInstance& instance = scene.get<PrefabInstance>(root);
    std::vector<PrefabOverride>& list = instance.overrides;
    const std::vector<PrefabOverride> restore = entriesFor(list, uid, component);

    // An override is a delta against the prefab's own value, so a component the
    // prefab does not define cannot carry one: the entry would be reported as
    // drift on the next load, and undoing it would restore nothing. Asked once,
    // as the first entry for this pair is about to be made, so a drag pays the
    // file read on its first frame rather than on every one.
    if (restore.empty() && !Prefab::definesComponent(instance.source, uid, component)) {
        return nullptr;
    }

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
        resources, root, uid, component, restore, std::move(entries), label);
}

void warnComponentIsPrefabs(const Scene& scene, EditorState& state, EntityId id,
                            const char* component, const char* fate) {
    if (!instanceRoot(scene, id)) return;
    state.pushToast(EditorState::ToastKind::Warning,
                    std::string("'") + component + "' " + fate
                        + " - Save as Prefab to put it in the prefab");
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

    // Dropping an override is a re-read of the prefab's definition, so there has
    // to be one left to re-read. A prefab that has since lost the component - or
    // the entity carrying it - has no value to hand the field back, and clearing
    // the entry on its own would leave the overridden number on screen with
    // nothing left saying it was ever an override.
    if (!Prefab::definesComponent(scene.get<PrefabInstance>(root).source, uid, component)) {
        state.pushToast(EditorState::ToastKind::Warning,
                        std::string("The prefab no longer defines ") + component
                            + " - the override is kept");
        return;
    }

    apply(scene, resources, root, uid, component, entries);
    state.commands.push(std::make_unique<PrefabOverrideCommand>(
        resources, root, uid, component, restore, std::move(entries), "Revert Override"));
    state.markSceneDirty();
}

} // namespace Engine::PrefabOverrides
