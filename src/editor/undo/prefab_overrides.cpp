#include "undo/prefab_overrides.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "ecs/scene.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"

#include "undo/command_stack.h"
#include "undo/composite_command.h"
#include "undo/entity_snapshot.h"
#include "undo/command_host.h"

namespace Vkm::Engine::PrefabOverrides {

namespace {

using nlohmann::json;

std::vector<PrefabOverride> entriesFor(
    const std::vector<PrefabOverride>& list,
    uint32_t uid,
    const std::string& component
) {
    std::vector<PrefabOverride> out;
    for (const PrefabOverride& o : list) {
        if (o.uid == uid && o.component == component) out.push_back(o);
    }
    return out;
}

void replaceEntries(
    std::vector<PrefabOverride>& list,
    uint32_t uid,
    const std::string& component,
    const std::vector<PrefabOverride>& entries
) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const PrefabOverride& o) {
        return o.uid == uid && o.component == component;
    }), list.end());
    list.insert(list.end(), entries.begin(), entries.end());
}

// Updates in place, so the inspector's list does not reshuffle during edits.
void setEntry(
    std::vector<PrefabOverride>& entries,
    uint32_t uid,
    const char* component,
    const std::string& field,
    std::string value
) {
    for (PrefabOverride& o : entries) {
        if (o.field != field) continue;
        o.value = std::move(value);
        return;
    }
    entries.push_back(PrefabOverride{uid, component, field, std::move(value)});
}

// HierarchyOperations' walk, which is bounded against a cycle.
EntityId entityWithUid(const Scene& scene, EntityId root, uint32_t uid) {
    return HierarchyOperations::findInSelfOrDescendantsIf(scene, root, [&](EntityId id) {
        const PrefabEntity* stamped = scene.tryGet<PrefabEntity>(id);
        return stamped && stamped->uid == uid;
    });
}

} // namespace

std::vector<std::string> overriddenFields(const Scene& scene, EntityId id, const char* component) {
    std::vector<std::string> fields;

    const EntityId root = Prefab::instanceRootOf(scene, id);
    const PrefabEntity* stamped = root ? scene.tryGet<PrefabEntity>(id) : nullptr;
    if (!stamped) return fields;

    const uint32_t uid = stamped->uid;

    // The root's Transform is the instance's own pose; the prefab never applies
    // an entry against it (Prefab::instantiateInto).
    if (uid == PrefabEntity::ROOT && std::strcmp(component, "Transform") == 0) return fields;

    for (const PrefabOverride& o : scene.get<PrefabInstance>(root).overrides) {
        if (o.uid == uid && o.component == component) fields.push_back(o.field);
    }
    return fields;
}

bool apply(
    Scene& scene,
    ResourceManager& resources,
    EntityId root,
    uint32_t uid,
    const std::string& component,
    const std::vector<PrefabOverride>& entries
) {
    if (!scene.isAlive(root) || !scene.has<PrefabInstance>(root)) return false;

    const EntityId target = entityWithUid(scene, root, uid);
    if (!target) return false;

    PrefabInstance& instance = scene.get<PrefabInstance>(root);
    replaceEntries(instance.overrides, uid, component, entries);

    return Prefab::reloadComponent(
        scene,
        resources,
        instance.source,
        target,
        uid,
        component,
        instance.overrides
    );
}

std::unique_ptr<Command> recordFields(
    Scene& scene,
    ResourceManager& resources,
    EntityId id,
    const char* component,
    const json& before,
    const json& after,
    const std::vector<std::string>& entities,
    const char* label
) {
    const EntityId root = Prefab::instanceRootOf(scene, id);
    const PrefabEntity* stamped = root ? scene.tryGet<PrefabEntity>(id) : nullptr;
    if (!stamped) return nullptr;

    const uint32_t uid = stamped->uid;

    // The root's Transform is the instance's own pose (Prefab::instantiateInto),
    // so an override on it could never take effect.
    if (uid == PrefabEntity::ROOT && std::strcmp(component, "Transform") == 0) return nullptr;

    PrefabInstance& instance = scene.get<PrefabInstance>(root);
    std::vector<PrefabOverride>& list = instance.overrides;
    const std::vector<PrefabOverride> restore = entriesFor(list, uid, component);

    // A component the prefab does not define cannot carry an override. Asked only
    // for this pair's first entry, so a drag reads the file on one frame.
    if (restore.empty() && !Prefab::definesComponent(instance.source, uid, component)) {
        return nullptr;
    }

    std::vector<PrefabOverride> entries = restore;
    bool changed = false;
    for (auto field = after.begin(); field != after.end(); ++field) {
        const auto was = before.find(field.key());
        if (was != before.end() && *was == field.value()) continue;
        // An entity field is skipped, so an edit changing nothing else records no override
        // and stays a plain edit the scene never stores.
        if (std::find(entities.begin(), entities.end(), field.key()) != entities.end()) continue;
        setEntry(entries, uid, component, field.key(), field.value().dump());
        changed = true;
    }
    if (!changed) return nullptr;

    // The live component already holds the typed value; re-reading it from the
    // prefab would put a file read in every frame of a drag.
    replaceEntries(list, uid, component, entries);

    return std::make_unique<PrefabOverrideCommand>(
        resources,
        root,
        uid,
        component,
        restore,
        std::move(entries),
        label
    );
}

void warnComponentIsPrefabs(
    const Scene& scene,
    CommandHost& host,
    EntityId id,
    const char* component,
    const char* fate
) {
    if (!Prefab::instanceRootOf(scene, id)) return;
    const std::string message = std::string("'") + component + "' " + fate
        + " - Save as Prefab to put it in the prefab";
    host.pushToast(ToastKind::Warning, message);
}

namespace {

// Drop the overrides of (@p id, @p component) that @p dropped names, giving those fields back the
// prefab's values, as one step.
template <typename Dropped>
void revertWhere(
    Scene& scene,
    ResourceManager& resources,
    CommandHost& host,
    EntityId id,
    const char* component,
    Dropped&& dropped
) {
    const EntityId root = Prefab::instanceRootOf(scene, id);
    const PrefabEntity* stamped = root ? scene.tryGet<PrefabEntity>(id) : nullptr;
    if (!stamped) return;

    const uint32_t uid = stamped->uid;
    const std::vector<PrefabOverride> restore =
        entriesFor(scene.get<PrefabInstance>(root).overrides, uid, component);

    std::vector<PrefabOverride> entries = restore;
    entries.erase(std::remove_if(entries.begin(), entries.end(), dropped), entries.end());
    if (entries.size() == restore.size()) return;

    if (!Prefab::definesComponent(scene.get<PrefabInstance>(root).source, uid, component)) {
        const std::string message = std::string("The prefab no longer defines ") + component
            + " - the override is kept";
        host.pushToast(ToastKind::Warning, message);
        return;
    }

    apply(scene, resources, root, uid, component, entries);
    host.pushStep(
        std::make_unique<PrefabOverrideCommand>(
            resources,
            root,
            uid,
            component,
            restore,
            std::move(entries),
            "Revert Override"
        )
    );
}

} // namespace

void revert(
    Scene& scene,
    ResourceManager& resources,
    CommandHost& host,
    EntityId id,
    const char* component,
    const std::string& field
) {
    const auto onField = [&](const PrefabOverride& o) { return o.field == field; };
    revertWhere(scene, resources, host, id, component, onField);
}

void revertComponent(
    Scene& scene,
    ResourceManager& resources,
    CommandHost& host,
    EntityId id,
    const char* component
) {
    revertWhere(scene, resources, host, id, component, [](const PrefabOverride&) { return true; });
}

void revertAll(Scene& scene, ResourceManager& resources, CommandHost& host, EntityId root) {
    const PrefabInstance* instance = scene.tryGet<PrefabInstance>(root);
    if (!instance || instance->overrides.empty()) return;

    // Each (entity, component) once: its entries go together.
    std::vector<std::pair<uint32_t, std::string>> pairs;
    for (const PrefabOverride& o : instance->overrides) {
        const std::pair<uint32_t, std::string> pair{o.uid, o.component};
        if (std::find(pairs.begin(), pairs.end(), pair) == pairs.end()) pairs.push_back(pair);
    }

    const std::string source = instance->source;
    auto step = std::make_unique<CompositeCommand>("Revert All Overrides");
    size_t kept = 0;
    for (const auto& [uid, component] : pairs) {
        if (!Prefab::definesComponent(source, uid, component)) {
            ++kept;
            continue;
        }
        const std::vector<PrefabOverride>& held = scene.get<PrefabInstance>(root).overrides;
        std::vector<PrefabOverride> restore     = entriesFor(held, uid, component);
        apply(scene, resources, root, uid, component, {});
        step->add(
            std::make_unique<PrefabOverrideCommand>(
                resources,
                root,
                uid,
                component,
                std::move(restore),
                std::vector<PrefabOverride>{},
                "Revert Override"
            )
        );
    }
    if (!step->empty()) host.pushStep(std::move(step));
    if (kept > 0) {
        host.pushToast(
            ToastKind::Warning,
            std::to_string(kept) + " override(s) are on components the prefab no longer defines - kept"
        );
    }
}

} // namespace Vkm::Engine::PrefabOverrides

namespace Vkm::Engine {

void PrefabOverrideCommand::step(
    Scene& scene,
    CommandHost& host,
    const std::vector<PrefabOverride>& entries
) {
    const bool applied = PrefabOverrides::apply(
        scene,
        *m_resources,
        liveEntity(scene, m_root),
        m_targetUid,
        m_component,
        entries
    );
    if (applied) return;
    const std::string message = "The prefab no longer defines " + m_component
        + " - the value here is stale until the scene is loaded again";
    host.pushToast(ToastKind::Warning, message);
}

void PrefabOverrideCommand::redo(Scene& scene, CommandHost& host) {
    step(scene, host, m_after);
}

void PrefabOverrideCommand::undo(Scene& scene, CommandHost& host) {
    step(scene, host, m_before);
}

bool PrefabOverrideCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<PrefabOverrideCommand*>(&incoming);
    if (!p || p->m_root != m_root || p->m_targetUid != m_targetUid
        || p->m_component != m_component) {
        return false;
    }
    m_after = p->m_after;
    return true;
}

} // namespace Vkm::Engine
