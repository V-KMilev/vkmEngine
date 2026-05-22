#include "framework/editor_commands.h"

#include "ecs/scene.h"
#include "framework/editor_state.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

// TransformChangeCommand

TransformChangeCommand::TransformChangeCommand(EntityId e,
                                               const Transform& before,
                                               const Transform& after,
                                               const char* label)
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

// AddComponentCommand<T>

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

// RemoveComponentCommand<T>

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

// Explicit instantiations - single TU emits the AddComponent / RemoveComponent
// machinery for each component type the editor mutates through commands.
template class AddComponentCommand<Mesh>;
template class AddComponentCommand<Light>;
template class AddComponentCommand<Camera>;
template class AddComponentCommand<Animation>;
template class AddComponentCommand<Name>;

template class RemoveComponentCommand<Mesh>;
template class RemoveComponentCommand<Light>;
template class RemoveComponentCommand<Camera>;
template class RemoveComponentCommand<Animation>;

// EntitySnapshot

EntitySnapshot EntitySnapshot::capture(const Scene& scene, EntityId id) {
    EntitySnapshot s;
    s.slotIndex = id.index;
    if (scene.has<Transform>(id)) s.transform = scene.get<Transform>(id);
    if (scene.has<Mesh>(id))      s.mesh      = scene.get<Mesh>(id);
    if (scene.has<Light>(id))     s.light     = scene.get<Light>(id);
    if (scene.has<Camera>(id))    s.camera    = scene.get<Camera>(id);
    if (scene.has<Animation>(id)) s.animation = scene.get<Animation>(id);
    if (scene.has<Name>(id))      s.name      = scene.get<Name>(id);
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
    if (animation && !scene.has<Animation>(id)) { Animation v = *animation; scene.add(e, std::move(v)); }
    if (name      && !scene.has<Name>(id))      { Name      v = *name;      scene.add(e, std::move(v)); }
}

// CreateEntityCommand

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

// DestroyEntityCommand

void DestroyEntityCommand::redo(Scene& scene, EditorState& state) {
    EntityId id{m_snap.slotIndex, scene.generationOf(m_snap.slotIndex)};
    if (!scene.isAlive(id)) return;
    scene.destroyEntity(Entity{id});
    state.hierarchyDirty = true;
    if (state.selectedEntity == id) state.selectedEntity = {};
}

void DestroyEntityCommand::undo(Scene& scene, EditorState& state) {
    Entity e = scene.createEntityAt(m_snap.slotIndex);
    m_snap.apply(scene, e.getID());
    state.hierarchyDirty = true;
    // Restore the selection that was active at destroy time, in case it
    // pointed at the resurrected entity (the common case).
    if (m_priorSelection.index == m_snap.slotIndex) {
        state.selectedEntity = e.getID();
    }
}

} // namespace Engine
