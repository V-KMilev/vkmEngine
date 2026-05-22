#pragma once

#include <optional>
#include <string>

#include "ecs/entity.h"
#include "ecs/component/transform.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"

#include "framework/command.h"

namespace Engine {

class Scene;
struct EditorState;

/**
 * @brief Reverts a Transform's position/rotation/scale.
 *
 * Captures before/after at construction. Coalesces consecutive Transform
 * changes on the same entity (so a continuous gizmo drag - or a stream of
 * inspector drag-float micro-edits - collapses to one undo step).
 */
class TransformChangeCommand : public Command {
    public:
        TransformChangeCommand(EntityId e, const Transform& before, const Transform& after,
                               const char* label);

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        EntityId    m_entity;
        Transform   m_before;
        Transform   m_after;
        const char* m_label;
};

/**
 * @brief Add a component of type T to an entity.
 *
 * Stored value is what gets re-added on redo. Undo removes it. Templated so
 * each component type (Mesh, Light, Camera, Animation, Name) gets its own
 * concrete command without runtime type erasure.
 */
template <typename T>
class AddComponentCommand : public Command {
    public:
        AddComponentCommand(EntityId e, T value, const char* label)
            : m_entity(e), m_value(std::move(value)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        EntityId    m_entity;
        T           m_value;
        const char* m_label;
};

/**
 * @brief Remove a component of type T from an entity.
 *
 * Captures the component value at construction so undo can re-add the
 * exact same data, not a default-constructed replacement.
 */
template <typename T>
class RemoveComponentCommand : public Command {
    public:
        RemoveComponentCommand(EntityId e, T snapshot, const char* label)
            : m_entity(e), m_snapshot(std::move(snapshot)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        EntityId    m_entity;
        T           m_snapshot;
        const char* m_label;
};

/**
 * @brief Snapshot of every editor-visible component on a single entity.
 *
 * Used by Create / Destroy commands so an undo can resurrect an entity
 * with the exact same components it had before destruction. Each field is
 * populated only when the entity carried that component. Future component
 * types added to the editor's vocabulary go here too.
 *
 * Hierarchy isn't snapshotted here - destroying a non-leaf entity isn't
 * an undoable action in the current implementation (would need a full
 * subtree snapshot).
 */
struct EntitySnapshot {
    uint32_t slotIndex = 0;
    std::optional<Transform> transform;
    std::optional<Mesh>      mesh;
    std::optional<Light>     light;
    std::optional<Camera>    camera;
    std::optional<Animation> animation;
    std::optional<Name>      name;

    static EntitySnapshot capture(const Scene& scene, EntityId id);
    void apply(Scene& scene, EntityId id) const;
};

/**
 * @brief Create a fresh entity with a fixed component set.
 *
 * Captures the post-create entity's slot index so redo can re-create at
 * the same slot (Scene::createEntityAt). Undo destroys.
 */
class CreateEntityCommand : public Command {
    public:
        CreateEntityCommand(EntitySnapshot snap, const char* label)
            : m_snap(std::move(snap)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        EntitySnapshot m_snap;
        const char*    m_label;
};

/**
 * @brief Destroy a leaf entity, capturing all its components for undo.
 *
 * Only safe for leaf entities (no Hierarchy with children). The editor
 * gates the destroy-with-children path through a different code path that
 * doesn't push an undo entry yet - future work.
 */
class DestroyEntityCommand : public Command {
    public:
        DestroyEntityCommand(EntitySnapshot snap, EntityId priorSelection, const char* label)
            : m_snap(std::move(snap)), m_priorSelection(priorSelection), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        EntitySnapshot m_snap;
        EntityId       m_priorSelection;
        const char*    m_label;
};

// Template instantiations are emitted in editor_commands.cpp so each
// translation unit doesn't need the full bodies.
extern template class AddComponentCommand<Mesh>;
extern template class AddComponentCommand<Light>;
extern template class AddComponentCommand<Camera>;
extern template class AddComponentCommand<Animation>;
extern template class AddComponentCommand<Name>;

extern template class RemoveComponentCommand<Mesh>;
extern template class RemoveComponentCommand<Light>;
extern template class RemoveComponentCommand<Camera>;
extern template class RemoveComponentCommand<Animation>;

} // namespace Engine
