#include "framework/editor_commands.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "ecs/component/lod.h"
#include "ecs/component/prefab_instance.h"
#include "ecs/scene.h"
#include "io/scene/component_serializer.h"
#include "io/scene/prefab.h"
#include "resource/resource_manager.h"
#include "framework/editor_state.h"
#include "framework/prefab_overrides.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {

// Re-resolve a captured entity id through its slot.
//
// Undoing a delete resurrects the entity at its original slot but with a
// fresh generation, so any older command still holding the pre-delete id
// would fail its isAlive guard and silently no-op forever after. The slot
// index is the half of the id that survives a resurrection, which is why
// Create/Destroy already key off it. Safe for the rest because the stack is
// LIFO: an older command can only run once everything pushed after it has
// been undone, and by then its slot holds the entity it was made against.
EntityId liveEntity(const Scene& scene, EntityId captured) {
    if (!captured || !scene.isAliveAtIndex(captured.index)) return {};
    return scene.entityAt(captured.index);
}

} // namespace

TransformChangeCommand::TransformChangeCommand(
    EntityId e,
    const Transform& before,
    const Transform& after,
    const char* label
)
    : m_entity(e), m_before(before), m_after(after), m_label(label) {}

void TransformChangeCommand::redo(Scene& scene, EditorState&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<Transform>(e)) return;
    scene.get<Transform>(e) = m_after;
}

void TransformChangeCommand::undo(Scene& scene, EditorState&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<Transform>(e)) return;
    scene.get<Transform>(e) = m_before;
}

bool TransformChangeCommand::tryMerge(Command& incoming) {
    // Coalesce only with another transform change on the same entity. The
    // chain start (m_before) stays put; only m_after slides forward to the
    // newest value the user landed on.
    auto* p = dynamic_cast<TransformChangeCommand*>(&incoming);
    if (!p || p->m_entity != m_entity) return false;
    m_after = p->m_after;
    return true;
}

EnvironmentEditCommand::EnvironmentEditCommand(
    const Environment& before, const Environment& after, const char* label)
    : m_before(before), m_after(after), m_label(label) {}

void EnvironmentEditCommand::redo(Scene& scene, EditorState&) {
    scene.environment() = m_after;
}

void EnvironmentEditCommand::undo(Scene& scene, EditorState&) {
    scene.environment() = m_before;
}

bool EnvironmentEditCommand::tryMerge(Command& incoming) {
    // Coalesce with any other Environment edit: the chain start (m_before)
    // stays; only m_after slides forward to the newest value.
    auto* p = dynamic_cast<EnvironmentEditCommand*>(&incoming);
    if (!p) return false;
    m_after = p->m_after;
    m_label = p->m_label;
    return true;
}

PhysicsSettingsEditCommand::PhysicsSettingsEditCommand(
    const PhysicsSettings& before, const PhysicsSettings& after, const char* label)
    : m_before(before), m_after(after), m_label(label) {}

void PhysicsSettingsEditCommand::redo(Scene& scene, EditorState&) {
    scene.physics() = m_after;
}

void PhysicsSettingsEditCommand::undo(Scene& scene, EditorState&) {
    scene.physics() = m_before;
}

bool PhysicsSettingsEditCommand::tryMerge(Command& incoming) {
    auto* p = dynamic_cast<PhysicsSettingsEditCommand*>(&incoming);
    if (!p) return false;
    m_after = p->m_after;
    m_label = p->m_label;
    return true;
}

template <typename T>
void AddComponentCommand<T>::redo(Scene& scene, EditorState& state) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || scene.has<T>(e)) return;
    // Pass a fresh copy via move - Scene::add's T&& is a forwarding
    // reference but an explicit-template-arg call would force rvalue bind.
    T copy = m_value;
    scene.add(e, std::move(copy));
    state.hierarchyDirty = true;
}

template <typename T>
void AddComponentCommand<T>::undo(Scene& scene, EditorState& state) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<T>(e)) return;
    scene.remove<T>(e);
    state.hierarchyDirty = true;
}

template <typename T>
void RemoveComponentCommand<T>::redo(Scene& scene, EditorState& state) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<T>(e)) return;
    scene.remove<T>(e);
    state.hierarchyDirty = true;
}

template <typename T>
void RemoveComponentCommand<T>::undo(Scene& scene, EditorState& state) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || scene.has<T>(e)) return;
    T copy = m_snapshot;
    scene.add(e, std::move(copy));
    state.hierarchyDirty = true;
}

template <typename T>
void ComponentEditCommand<T>::redo(Scene& scene, EditorState&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<T>(e)) return;
    scene.get<T>(e) = m_after;
}

template <typename T>
void ComponentEditCommand<T>::undo(Scene& scene, EditorState&) {
    const EntityId e = liveEntity(scene, m_entity);
    if (!e || !scene.has<T>(e)) return;
    scene.get<T>(e) = m_before;
}

template <typename T>
bool ComponentEditCommand<T>::tryMerge(Command& incoming) {
    // Coalesce only with another edit of the same component type on the same
    // entity. The chain start (m_before) stays; only m_after slides forward.
    auto* p = dynamic_cast<ComponentEditCommand<T>*>(&incoming);
    if (!p || p->m_entity != m_entity) return false;
    m_after = p->m_after;
    return true;
}

// One TU emits the Add / Remove / Edit machinery for every component type on
// the list, so no other translation unit needs the bodies.
#define VKM_EDITOR_INSTANTIATE_COMMAND(Type)     \
    template class AddComponentCommand<Type>;    \
    template class RemoveComponentCommand<Type>; \
    template class ComponentEditCommand<Type>;
VKM_EDITOR_COMMAND_COMPONENTS(VKM_EDITOR_INSTANTIATE_COMMAND)
#undef VKM_EDITOR_INSTANTIATE_COMMAND

// The editor never offers removing a Name, so Name gets Add and Edit only.
template class AddComponentCommand<Name>;
template class ComponentEditCommand<Name>;

template <typename HandleType>
void RenameAssetCommand<HandleType>::redo(Scene&, EditorState& state) {
    if (!m_resources->isAlive(m_handle)) return;  // deleted since - no-op
    m_resources->rename(m_handle, m_after);
    state.markSceneDirty();
}

template <typename HandleType>
void RenameAssetCommand<HandleType>::undo(Scene&, EditorState& state) {
    if (!m_resources->isAlive(m_handle)) return;  // deleted since - no-op
    m_resources->rename(m_handle, m_before);
    state.markSceneDirty();
}

template class RenameAssetCommand<MaterialHandle>;
template class RenameAssetCommand<MeshHandle>;

EntitySnapshot EntitySnapshot::capture(const Scene& scene, EntityId id) {
    EntitySnapshot s;
    s.slotIndex = id.index;
#define VKM_SNAPSHOT_CAPTURE(Type, field) \
    if (scene.has<Type>(id)) s.field = scene.get<Type>(id);
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_CAPTURE)
#undef VKM_SNAPSHOT_CAPTURE
    if (scene.has<ScriptComponent>(id)) {
        s.scriptJson = ComponentSerializer::save(scene.get<ScriptComponent>(id)).dump();
    }
    return s;
}

void EntitySnapshot::apply(Scene& scene, EntityId id) const {
    // Construct a fresh copy before forwarding - Scene::add's T&& binds an
    // rvalue and the optionals hold lvalues. Only add what's missing so apply
    // onto a half-populated entity is a no-op for components already present.
#define VKM_SNAPSHOT_APPLY(Type, fieldName) \
    if (fieldName && !scene.has<Type>(id)) { Type v = *fieldName; scene.add(id, std::move(v)); }
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_APPLY)
#undef VKM_SNAPSHOT_APPLY
    if (scriptJson && !scene.has<ScriptComponent>(id)) {
        ScriptComponent sc;
        ComponentSerializer::load(nlohmann::json::parse(*scriptJson), sc);
        scene.add(id, std::move(sc));
    }
}

void CreateEntityCommand::redo(Scene& scene, EditorState& state) {
    EntityId e = scene.createEntityAt(m_snap.slotIndex);
    m_snap.apply(scene, e);
    if (m_parentSlot && scene.isAliveAtIndex(m_parentSlot)) {
        const EntityId parent = scene.entityAt(m_parentSlot);
        HierarchyOperations::setParent(scene, e, parent);
    }
    state.hierarchyDirty = true;
    state.selectEntity(e);
}

void CreateEntityCommand::undo(Scene& scene, EditorState& state) {
    EntityId id = scene.entityAt(m_snap.slotIndex);
    if (!scene.isAlive(id)) return;
    scene.destroyEntity(id);
    state.hierarchyDirty = true;
    if (state.selectedEntity == id) state.deselect();
}

SubtreeSnapshot SubtreeSnapshot::capture(const Scene& scene, EntityId root) {
    SubtreeSnapshot s;
    if (!scene.isAlive(root)) return s;
    if (scene.has<Hierarchy>(root)) {
        s.rootParentSlot = scene.get<Hierarchy>(root).parent.index;
    }
    // DFS pre-order. Stack pushes children in reverse so the original
    // firstChild->nextSibling order pops out left-to-right; the nodes
    // vector therefore lists parents before their children.
    struct Frame { EntityId id; uint32_t parentSlot; };
    std::vector<Frame> stack;
    stack.push_back({root, 0});
    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        if (!scene.isAlive(f.id)) continue;

        Node n;
        n.snap = EntitySnapshot::capture(scene, f.id);
        n.parentSlot = f.parentSlot;
        s.nodes.push_back(std::move(n));

        if (!scene.has<Hierarchy>(f.id)) continue;
        const auto& h = scene.get<Hierarchy>(f.id);

        // Collect children left-to-right, then push reversed so DFS pops
        // them in their original sibling order.
        std::vector<EntityId> children;
        EntityId c = h.firstChild;
        while (c) {
            if (!scene.isAlive(c) || !scene.has<Hierarchy>(c)) break;
            children.push_back(c);
            c = scene.get<Hierarchy>(c).nextSibling;
        }
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push_back({*it, f.id.index});
        }
    }
    return s;
}

void SubtreeSnapshot::apply(Scene& scene) const {
    // Pass 1: recreate every entity at its original slot and re-add its
    // components. Slot recycling means generations are bumped, but the
    // slot index is stable.
    for (const auto& node : nodes) {
        EntityId e = scene.createEntityAt(node.snap.slotIndex);
        node.snap.apply(scene, e);
    }
    // Pass 2: link parents. setParent PREPENDS to the parent's child list,
    // so to restore the original firstChild-first order we walk the nodes
    // in reverse - the last-captured (rightmost) child links first, the
    // first-captured (leftmost) child links last and ends up at firstChild.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const auto& node = *it;
        EntityId child = scene.entityAt(node.snap.slotIndex);
        if (!scene.isAlive(child)) continue;

        const uint32_t parentSlot = (node.parentSlot != 0) ? node.parentSlot : rootParentSlot;
        if (parentSlot == 0) continue;  // top-level
        EntityId parent = scene.entityAt(parentSlot);
        if (!scene.isAlive(parent)) continue;  // external parent died in the meantime
        HierarchyOperations::setParent(scene, child, parent);
    }
}

void DestroySubtreeCommand::redo(Scene& scene, EditorState& state) {
    if (m_snap.nodes.empty()) return;
    const uint32_t rootSlot = m_snap.nodes.front().snap.slotIndex;
    EntityId root = scene.entityAt(rootSlot);
    if (!scene.isAlive(root)) return;
    HierarchyOperations::destroyHierarchy(scene, root);
    state.hierarchyDirty = true;
    if (state.selectedEntity.index == rootSlot) state.deselect();
}

void DestroySubtreeCommand::undo(Scene& scene, EditorState& state) {
    m_snap.apply(scene);
    state.hierarchyDirty = true;
    if (m_priorSelection.index != 0) {
        // Restore selection if the prior pick was anywhere inside the
        // resurrected subtree (the common case is the root itself).
        for (const auto& node : m_snap.nodes) {
            if (node.snap.slotIndex == m_priorSelection.index) {
                state.selectEntity(scene.entityAt(node.snap.slotIndex));
                break;
            }
        }
    }
}

void PlacePrefabCommand::redo(Scene& scene, EditorState& state) {
    // Rebuilt into a root reclaimed at its original slot, the way the scene
    // loader restores an instance: a command pushed after this one addresses
    // the placed entity by index, so the index has to still be that entity's.
    const EntityId root = scene.createEntityAt(m_rootSlot);

    // The pose goes on first because instantiateInto keeps a Transform the root
    // already carries and takes the prefab's authored one otherwise.
    scene.add(root, Transform{m_at});
    scene.add(root, PrefabInstance{m_instance});

    if (!Prefab::instantiateInto(scene, *m_resources, m_instance.source, root,
                                 m_instance.overrides)) {
        // The subtree, not the root: a build that stopped partway has already
        // parented whatever it managed to create under it.
        HierarchyOperations::destroyHierarchy(scene, root);
        // The prefab is read again on every rebuild, so it can be gone or
        // unreadable by the time a redo asks for it. Without this the redo is a
        // keypress that does nothing to a scene that visibly lost an entity.
        const std::string name = std::filesystem::path(m_instance.source).filename().string();
        state.pushToast(EditorState::ToastKind::Error,
                        "Could not rebuild the instance of '" + name + "'");
        return;
    }

    state.hierarchyDirty = true;
    state.selectEntity(root);
}

void PlacePrefabCommand::undo(Scene& scene, EditorState& state) {
    const EntityId root = scene.entityAt(m_rootSlot);
    if (!scene.isAlive(root)) return;
    HierarchyOperations::destroyHierarchy(scene, root);
    state.hierarchyDirty = true;
    if (state.selectedEntity.index == m_rootSlot) state.deselect();
}

// The entry list is what the scene stores, so it moves either way; the value
// beside it comes back from the prefab, and a prefab that has since lost the
// component has none to give. Say so rather than leave the number on screen
// disagreeing with the list that no longer claims it.
void PrefabOverrideCommand::step(Scene& scene, EditorState& state,
                                 const std::vector<PrefabOverride>& entries) {
    if (PrefabOverrides::apply(scene, *m_resources, liveEntity(scene, m_root),
                               m_targetUid, m_component, entries)) {
        return;
    }
    state.pushToast(EditorState::ToastKind::Warning,
                    "The prefab no longer defines " + m_component
                        + " - the value here is stale until the scene is loaded again");
}

void PrefabOverrideCommand::redo(Scene& scene, EditorState& state) {
    step(scene, state, m_after);
}

void PrefabOverrideCommand::undo(Scene& scene, EditorState& state) {
    step(scene, state, m_before);
}

bool PrefabOverrideCommand::tryMerge(Command& incoming) {
    // Coalesce only with another change to the same entity's same component -
    // one drag over one card. The chain start (m_before) stays; only m_after
    // slides forward to the newest entry set.
    auto* p = dynamic_cast<PrefabOverrideCommand*>(&incoming);
    if (!p || p->m_root != m_root || p->m_targetUid != m_targetUid
        || p->m_component != m_component) {
        return false;
    }
    m_after = p->m_after;
    return true;
}

namespace {
// Re-link `child` under `parent` (null = root) and restore the stored local
// transform, so undo/redo land on the exact world-preserving state the
// interactive reparent produced.
void applyReparent(Scene& scene, EntityId child, EntityId capturedParent, const Transform& local) {
    if (capturedParent) {
        const EntityId parent = liveEntity(scene, capturedParent);
        if (!parent) return;
        HierarchyOperations::setParent(scene, child, parent);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }
    if (scene.has<Transform>(child)) scene.get<Transform>(child) = local;
}
} // namespace

void ReparentCommand::redo(Scene& scene, EditorState& state) {
    const EntityId child = liveEntity(scene, m_child);
    if (!child) return;
    applyReparent(scene, child, m_newParent, m_after);
    state.hierarchyDirty = true;
}

void ReparentCommand::undo(Scene& scene, EditorState& state) {
    const EntityId child = liveEntity(scene, m_child);
    if (!child) return;
    applyReparent(scene, child, m_oldParent, m_before);
    state.hierarchyDirty = true;
}

void SetActiveCameraCommand::redo(Scene& scene, EditorState&) {
    // Compare by slot: m_before is keyed by slot for the same reason, and the
    // target may have been resurrected since with a newer generation.
    scene.forEach<Camera>([&](EntityId id, Camera& c) { c.active = (id.index == m_target.index); });
}

void SetActiveCameraCommand::undo(Scene& scene, EditorState&) {
    for (const auto& [slot, wasActive] : m_before) {
        EntityId id = scene.entityAt(slot);
        if (scene.isAlive(id) && scene.has<Camera>(id)) scene.get<Camera>(id).active = wasActive;
    }
}

} // namespace Vkm::Engine
