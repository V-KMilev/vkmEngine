#include "framework/editor_commands.h"

#include <algorithm>

#include "ecs/scene.h"
#include "ecs/component/reflection_probe.h"
#include "resource/resource_manager.h"
#include "framework/editor_state.h"
#include "system/hierarchy/hierarchy_operations.h"

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
template class AddComponentCommand<ReflectionProbe>;
template class AddComponentCommand<Camera>;
template class AddComponentCommand<Animation>;
template class AddComponentCommand<Name>;

template class RemoveComponentCommand<Mesh>;
template class RemoveComponentCommand<Light>;
template class RemoveComponentCommand<ReflectionProbe>;
template class RemoveComponentCommand<Camera>;
template class RemoveComponentCommand<Animation>;

template class AddComponentCommand<MeshLOD>;
template class RemoveComponentCommand<MeshLOD>;

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
template class ComponentEditCommand<ReflectionProbe>;
template class ComponentEditCommand<Mesh>;
template class ComponentEditCommand<Name>;
template class ComponentEditCommand<Animation>;

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
    if (scene.has<Transform>(id)) s.transform = scene.get<Transform>(id);
    if (scene.has<Mesh>(id))      s.mesh      = scene.get<Mesh>(id);
    if (scene.has<Light>(id))     s.light     = scene.get<Light>(id);
    if (scene.has<Camera>(id))    s.camera    = scene.get<Camera>(id);
    if (scene.has<ReflectionProbe>(id)) s.reflectionProbe = scene.get<ReflectionProbe>(id);
    if (scene.has<Animation>(id)) s.animation = scene.get<Animation>(id);
    if (scene.has<Name>(id))      s.name      = scene.get<Name>(id);
    if (scene.has<MeshLOD>(id))   s.meshLod   = scene.get<MeshLOD>(id);
    return s;
}

void EntitySnapshot::apply(Scene& scene, EntityId id) const {
    Entity e{id};
    // Construct fresh copies before forwarding - Scene::add's T&& binds
    // an rvalue and the optionals hold lvalues.
    if (transform && !scene.has<Transform>(id)) { Transform v = *transform; scene.add(e, std::move(v)); }
    if (mesh      && !scene.has<Mesh>(id))      { Mesh      v = *mesh;      scene.add(e, std::move(v)); }
    if (light     && !scene.has<Light>(id))     { Light     v = *light;     scene.add(e, std::move(v)); }
    if (camera    && !scene.has<Camera>(id))    { Camera    v = *camera;    scene.add(e, std::move(v)); }
    if (reflectionProbe && !scene.has<ReflectionProbe>(id)) { ReflectionProbe v = *reflectionProbe; scene.add(e, std::move(v)); }
    if (animation && !scene.has<Animation>(id)) { Animation v = *animation; scene.add(e, std::move(v)); }
    if (name      && !scene.has<Name>(id))      { Name      v = *name;      scene.add(e, std::move(v)); }
    if (meshLod   && !scene.has<MeshLOD>(id))   { MeshLOD   v = *meshLod;   scene.add(e, std::move(v)); }
}

void CreateEntityCommand::redo(Scene& scene, EditorState& state) {
    Entity e = scene.createEntityAt(m_snap.slotIndex);
    m_snap.apply(scene, e.getID());
    state.hierarchyDirty = true;
    state.selectedEntity = e.getID();
}

void CreateEntityCommand::undo(Scene& scene, EditorState& state) {
    EntityId id{m_snap.slotIndex, scene.generationOf(m_snap.slotIndex)};
    if (!scene.isAlive(id)) return;
    scene.destroyEntity(Entity{id});
    state.hierarchyDirty = true;
    if (state.selectedEntity == id) state.selectedEntity = {};
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
    if (state.selectedEntity.index == rootSlot) state.selectedEntity = {};
}

void DestroySubtreeCommand::undo(Scene& scene, EditorState& state) {
    m_snap.apply(scene);
    state.hierarchyDirty = true;
    if (m_priorSelection.index != 0) {
        // Restore selection if the prior pick was anywhere inside the
        // resurrected subtree (the common case is the root itself).
        for (const auto& node : m_snap.nodes) {
            if (node.snap.slotIndex == m_priorSelection.index) {
                state.selectedEntity = EntityId{node.snap.slotIndex,
                    scene.generationOf(node.snap.slotIndex)};
                break;
            }
        }
    }
}

void ReparentCommand::redo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_child)) return;
    if (m_newParent) {
        if (!scene.isAlive(m_newParent)) return;
        HierarchyOperations::setParent(scene, m_child, m_newParent);
    } else {
        HierarchyOperations::removeFromParent(scene, m_child);
    }
    state.hierarchyDirty = true;
}

void ReparentCommand::undo(Scene& scene, EditorState& state) {
    if (!scene.isAlive(m_child)) return;
    if (m_oldParent) {
        if (!scene.isAlive(m_oldParent)) return;
        HierarchyOperations::setParent(scene, m_child, m_oldParent);
    } else {
        HierarchyOperations::removeFromParent(scene, m_child);
    }
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
