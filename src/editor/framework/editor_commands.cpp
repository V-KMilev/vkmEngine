#include "framework/editor_commands.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "ecs/component/lod.h"
#include "ecs/scene.h"
#include "io/scene/component_serializer.h"
#include "resource/resource_manager.h"
#include "framework/editor_state.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/script/script_component.h"

namespace Engine {

TransformChangeCommand::TransformChangeCommand(
    EntityId e,
    const Transform& before,
    const Transform& after,
    const char* label
)
    : m_entity(e), m_before(before), m_after(after), m_label(label) {}

void TransformChangeCommand::redo(Scene& scene, EditorState&) {
    if (!scene.isAlive(m_entity) || !scene.has<Transform>(m_entity)) return;
    scene.get<Transform>(m_entity) = m_after;
    HierarchyOperations::markDirty(scene, m_entity);
}

void TransformChangeCommand::undo(Scene& scene, EditorState&) {
    if (!scene.isAlive(m_entity) || !scene.has<Transform>(m_entity)) return;
    scene.get<Transform>(m_entity) = m_before;
    HierarchyOperations::markDirty(scene, m_entity);
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
    if (!scene.isAlive(m_entity) || scene.has<T>(m_entity)) return;
    // Pass a fresh copy via move - Scene::add's T&& is a forwarding
    // reference but an explicit-template-arg call would force rvalue bind.
    T copy = m_value;
    scene.add(Entity{m_entity}, std::move(copy));
    state.hierarchyDirty = true;
}

template <typename T>
void AddComponentCommand<T>::undo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_entity) || !scene.has<T>(m_entity)) return;
    scene.remove<T>(Entity{m_entity});
    state.hierarchyDirty = true;
}

template <typename T>
void RemoveComponentCommand<T>::redo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_entity) || !scene.has<T>(m_entity)) return;
    scene.remove<T>(Entity{m_entity});
    state.hierarchyDirty = true;
}

template <typename T>
void RemoveComponentCommand<T>::undo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_entity) || scene.has<T>(m_entity)) return;
    T copy = m_snapshot;
    scene.add(Entity{m_entity}, std::move(copy));
    state.hierarchyDirty = true;
}

// Single TU emits the AddComponent / RemoveComponent machinery for each
// component type the editor mutates through commands.
template class AddComponentCommand<Mesh>;
template class AddComponentCommand<Light>;
template class AddComponentCommand<Camera>;
template class AddComponentCommand<Animation>;
template class AddComponentCommand<Name>;

template class RemoveComponentCommand<Mesh>;
template class RemoveComponentCommand<Light>;
template class RemoveComponentCommand<Camera>;
template class RemoveComponentCommand<Animation>;


template class AddComponentCommand<Rigidbody>;
template class RemoveComponentCommand<Rigidbody>;

template class AddComponentCommand<Collider>;
template class RemoveComponentCommand<Collider>;

template class AddComponentCommand<ReflectionProbe>;
template class RemoveComponentCommand<ReflectionProbe>;

template class AddComponentCommand<Decal>;
template class RemoveComponentCommand<Decal>;

template class AddComponentCommand<ParticleEmitter>;
template class RemoveComponentCommand<ParticleEmitter>;

template class AddComponentCommand<IrradianceVolume>;
template class RemoveComponentCommand<IrradianceVolume>;
template class AddComponentCommand<LOD>;
template class RemoveComponentCommand<LOD>;

template <typename T>
void ComponentEditCommand<T>::redo(Scene& scene, EditorState&) {
    if (!scene.isAlive(m_entity) || !scene.has<T>(m_entity)) return;
    scene.get<T>(m_entity) = m_after;
}

template <typename T>
void ComponentEditCommand<T>::undo(Scene& scene, EditorState&) {
    if (!scene.isAlive(m_entity) || !scene.has<T>(m_entity)) return;
    scene.get<T>(m_entity) = m_before;
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

template class ComponentEditCommand<Light>;
template class ComponentEditCommand<Camera>;
template class ComponentEditCommand<Mesh>;
template class ComponentEditCommand<Name>;
template class ComponentEditCommand<Animation>;
template class ComponentEditCommand<Rigidbody>;
template class ComponentEditCommand<Collider>;
template class ComponentEditCommand<ReflectionProbe>;
template class ComponentEditCommand<Decal>;
template class ComponentEditCommand<ParticleEmitter>;
template class ComponentEditCommand<IrradianceVolume>;
template class ComponentEditCommand<LOD>;

template class AddComponentCommand<UICanvas>;
template class AddComponentCommand<UIElement>;
template class AddComponentCommand<UIImage>;
template class AddComponentCommand<UIText>;
template class AddComponentCommand<UIButton>;
template class RemoveComponentCommand<UICanvas>;
template class RemoveComponentCommand<UIElement>;
template class RemoveComponentCommand<UIImage>;
template class RemoveComponentCommand<UIText>;
template class RemoveComponentCommand<UIButton>;
template class ComponentEditCommand<UICanvas>;
template class ComponentEditCommand<UIElement>;
template class ComponentEditCommand<UIImage>;
template class ComponentEditCommand<UIText>;
template class ComponentEditCommand<UIButton>;

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
    Entity e{id};
    // Construct a fresh copy before forwarding - Scene::add's T&& binds an
    // rvalue and the optionals hold lvalues. Only add what's missing so apply
    // onto a half-populated entity is a no-op for components already present.
#define VKM_SNAPSHOT_APPLY(Type, fieldName) \
    if (fieldName && !scene.has<Type>(id)) { Type v = *fieldName; scene.add(e, std::move(v)); }
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_APPLY)
#undef VKM_SNAPSHOT_APPLY
    if (scriptJson && !scene.has<ScriptComponent>(id)) {
        ScriptComponent sc;
        ComponentSerializer::load(nlohmann::json::parse(*scriptJson), sc);
        scene.add(e, std::move(sc));
    }
}

void CreateEntityCommand::redo(Scene& scene, EditorState& state) {
    Entity e = scene.createEntityAt(m_snap.slotIndex);
    m_snap.apply(scene, e.getID());
    if (m_parentSlot && scene.isAliveAtIndex(m_parentSlot)) {
        const EntityId parent{m_parentSlot, scene.generationOf(m_parentSlot)};
        HierarchyOperations::setParent(scene, e.getID(), parent);
    }
    state.hierarchyDirty = true;
    state.selectEntity(e.getID());
}

void CreateEntityCommand::undo(Scene& scene, EditorState& state) {
    EntityId id{m_snap.slotIndex, scene.generationOf(m_snap.slotIndex)};
    if (!scene.isAlive(id)) return;
    scene.destroyEntity(Entity{id});
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
        Entity e = scene.createEntityAt(node.snap.slotIndex);
        node.snap.apply(scene, e.getID());
    }
    // Pass 2: link parents. setParent PREPENDS to the parent's child list,
    // so to restore the original firstChild-first order we walk the nodes
    // in reverse - the last-captured (rightmost) child links first, the
    // first-captured (leftmost) child links last and ends up at firstChild.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const auto& node = *it;
        EntityId child{node.snap.slotIndex, scene.generationOf(node.snap.slotIndex)};
        if (!scene.isAlive(child)) continue;

        const uint32_t parentSlot = (node.parentSlot != 0) ? node.parentSlot : rootParentSlot;
        if (parentSlot == 0) continue;  // top-level
        EntityId parent{parentSlot, scene.generationOf(parentSlot)};
        if (!scene.isAlive(parent)) continue;  // external parent died in the meantime
        HierarchyOperations::setParent(scene, child, parent);
    }
}

void DestroySubtreeCommand::redo(Scene& scene, EditorState& state) {
    if (m_snap.nodes.empty()) return;
    const uint32_t rootSlot = m_snap.nodes.front().snap.slotIndex;
    EntityId root{rootSlot, scene.generationOf(rootSlot)};
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
                state.selectEntity(EntityId{node.snap.slotIndex,
                    scene.generationOf(node.snap.slotIndex)});
                break;
            }
        }
    }
}

namespace {
// Re-link `child` under `parent` (null = root) and restore the stored local
// transform, so undo/redo land on the exact world-preserving state the
// interactive reparent produced.
void applyReparent(Scene& scene, EntityId child, EntityId parent, const Transform& local) {
    if (parent) {
        if (!scene.isAlive(parent)) return;
        HierarchyOperations::setParent(scene, child, parent);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }
    if (scene.has<Transform>(child)) scene.get<Transform>(child) = local;
    HierarchyOperations::markDirty(scene, child);
}
} // namespace

void ReparentCommand::redo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_child)) return;
    applyReparent(scene, m_child, m_newParent, m_after);
    state.hierarchyDirty = true;
}

void ReparentCommand::undo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_child)) return;
    applyReparent(scene, m_child, m_oldParent, m_before);
    state.hierarchyDirty = true;
}

void SetActiveCameraCommand::redo(Scene& scene, EditorState&) {
    scene.forEach<Camera>([&](EntityId id, Camera& c) { c.active = (id == m_target); });
}

void SetActiveCameraCommand::undo(Scene& scene, EditorState&) {
    for (const auto& [slot, wasActive] : m_before) {
        EntityId id{slot, scene.generationOf(slot)};
        if (scene.isAlive(id) && scene.has<Camera>(id)) scene.get<Camera>(id).active = wasActive;
    }
}

} // namespace Engine
