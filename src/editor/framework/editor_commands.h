#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "ecs/component/transform.h"
#include "ecs/environment.h"
#include "ecs/component/mesh.h"
#include "ecs/component/lod.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/collider.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/decal.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/ui_canvas.h"
#include "ecs/component/ui_element.h"
#include "ecs/component/ui_image.h"
#include "ecs/component/ui_text.h"
#include "ecs/component/ui_button.h"

#include "framework/command.h"

namespace Engine {

class Scene;
struct EditorState;
class ResourceManager;

/**
 * @brief Reverts a Transform's position/rotation/scale.
 *
 * Captures before/after at construction. Coalesces consecutive Transform
 * changes on the same entity (so a continuous gizmo drag - or a stream of
 * inspector drag-float micro-edits - collapses to one undo step).
 */
class TransformChangeCommand : public Command {
    public:
        TransformChangeCommand(
            EntityId e,
            const Transform& before,
            const Transform& after,
            const char* label
        );

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
 * @brief Reverts an edit of the scene-global Environment (sky, fog, physics).
 *
 * The Environment is a copyable value on the Scene rather than a component,
 * so ComponentEditCommand cannot cover it. Coalesces consecutive Environment
 * edits, so a slider drag in the World inspector collapses to one undo step.
 */
class EnvironmentEditCommand : public Command {
    public:
        EnvironmentEditCommand(
            const Environment& before,
            const Environment& after,
            const char* label
        );

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        Environment m_before;
        Environment m_after;
        const char* m_label;
};

/**
 * @brief A group of already-applied commands undone/redone as one step.
 *
 * Batch operations over a multi-selection (delete, duplicate, gizmo drags)
 * build one of these from their per-entity commands: redo replays in order,
 * undo reverses in reverse order, and the whole batch is a single entry in
 * the history.
 */
class CompositeCommand : public Command {
    public:
        explicit CompositeCommand(const char* label) : m_label(label) {}

        /**
         * @brief Append an already-applied sub-command; execution order is
         * append order.
         */
        void add(std::unique_ptr<Command> cmd) { m_commands.push_back(std::move(cmd)); }

        bool empty() const { return m_commands.empty(); }

        void redo(Scene& scene, EditorState& state) override {
            for (auto& c : m_commands) c->redo(scene, state);
        }
        void undo(Scene& scene, EditorState& state) override {
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
                (*it)->undo(scene, state);
        }
        const char* label() const override { return m_label; }

    private:
        std::vector<std::unique_ptr<Command>> m_commands;
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
 * @brief Undoable edit of a whole component's value (before -> after).
 *
 * For inspector field edits on pure-data components. redo/undo assign the
 * stored value back; tryMerge coalesces a stream of same-entity, same-type
 * edits (the inspector pushes one per changed frame during a drag) into one
 * undo step, mirroring TransformChangeCommand. Use only for components whose
 * edit has no cross-entity or re-bake side effect (Transform has its own
 * command for the hierarchy-dirty side effect).
 */
template <typename T>
class ComponentEditCommand : public Command {
    public:
        ComponentEditCommand(EntityId e, const T& before, const T& after, const char* label)
            : m_entity(e), m_before(before), m_after(after), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        EntityId    m_entity;
        T           m_before;
        T           m_after;
        const char* m_label;
};

/**
 * @brief The value-copyable components an EntitySnapshot round-trips, as
 * (Type, field-name) rows.
 *
 * This single list drives the snapshot's fields, capture() and apply() so the
 * three can never drift - adding a component to the editor's "resurrect intact"
 * vocabulary is one new row here. ScriptComponent is deliberately absent: it is
 * move-only and stored as serialized JSON (see EntitySnapshot::scriptJson),
 * handled as an explicit special case in capture/apply.
 */
#define VKM_EDITOR_SNAPSHOT_COMPONENTS(X) \
    X(Transform,       transform)         \
    X(Mesh,            mesh)              \
    X(LOD,             lod)               \
    X(Light,           light)            \
    X(Camera,          camera)           \
    X(Animation,       animation)        \
    X(Name,            name)             \
    X(Rigidbody,        rigidbody)        \
    X(Collider,         collider)         \
    X(ReflectionProbe,  reflectionProbe)  \
    X(IrradianceVolume, irradianceVolume) \
    X(Decal,            decal)            \
    X(ParticleEmitter,  particleEmitter)  \
    X(UICanvas,         uiCanvas)         \
    X(UIElement,        uiElement)        \
    X(UIImage,          uiImage)          \
    X(UIText,           uiText)           \
    X(UIButton,         uiButton)

/**
 * @brief Snapshot of every editor-visible component on a single entity.
 *
 * Used by Create / Destroy commands so an undo can resurrect an entity
 * with the exact same components it had before destruction. Each field is
 * populated only when the entity carried that component. The component set is
 * defined once in VKM_EDITOR_SNAPSHOT_COMPONENTS above.
 *
 * Hierarchy itself is not stored here - subtree rewiring is the job of
 * SubtreeSnapshot, which knows the parent/child links across multiple
 * entities. Single-entity snapshots are only used for leaf operations.
 */
struct EntitySnapshot {
    uint32_t slotIndex = 0;
#define VKM_SNAPSHOT_FIELD(Type, field) std::optional<Type> field;
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_FIELD)
#undef VKM_SNAPSHOT_FIELD
    /**
     * @brief ScriptComponent is move-only, so it can't be stored as a value here -
     * it's kept as its serialized JSON (type names + reflected fields) and
     * recreated on apply via the registry-backed ComponentSerializer. Keeps
     * EntitySnapshot copyable.
     */
    std::optional<std::string>     scriptJson;

    static EntitySnapshot capture(const Scene& scene, EntityId id);
    void apply(Scene& scene, EntityId id) const;
};

/**
 * @brief Snapshot of a whole subtree (entity + every descendant).
 *
 * Captures each entity's components plus enough hierarchy info to recreate
 * the parent/child links exactly. Nodes are stored in DFS pre-order so that
 * a recreate pass can call setParent in the order parents-before-children.
 *
 * Used by DestroySubtreeCommand to make non-leaf entity deletion undoable.
 */
struct SubtreeSnapshot {
    struct Node {
        EntitySnapshot snap;
        /**
         * @brief Slot of this node's parent within the subtree (0 if this is the
         * subtree root). Distinct from rootParentSlot, which records the
         * external parent the root had before destruction.
         */
        uint32_t parentSlot = 0;
    };
    std::vector<Node> nodes;
    /**
     * @brief Slot of the original parent of the subtree's root (0 if the root
     * was top-level). On undo, the root is reattached to this entity.
     */
    uint32_t rootParentSlot = 0;

    static SubtreeSnapshot capture(const Scene& scene, EntityId root);
    void apply(Scene& scene) const;
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
 * @brief Destroy a whole subtree (entity + every descendant), undoable.
 *
 * Captures every entity in the subtree along with their parent/child wiring
 * so that undo can re-create the entire structure. The captured slot indices
 * are reused via Scene::createEntityAt; generations are re-issued naturally.
 */
class DestroySubtreeCommand : public Command {
    public:
        DestroySubtreeCommand(SubtreeSnapshot snap, EntityId priorSelection, const char* label)
            : m_snap(std::move(snap)), m_priorSelection(priorSelection), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        SubtreeSnapshot m_snap;
        EntityId        m_priorSelection;
        const char*     m_label;
};

/**
 * @brief Re-parent an entity, undoable.
 *
 * Records (child, oldParent, newParent) plus the child's local Transform on
 * each side of the move; either parent may be a null EntityId for "top-level".
 * The interactive reparent re-bases the local Transform so world position is
 * preserved, so undo/redo restore the matching transform alongside the links.
 */
class ReparentCommand : public Command {
    public:
        ReparentCommand(EntityId child, EntityId oldParent, EntityId newParent,
                        const Transform& before, const Transform& after, const char* label)
            : m_child(child), m_oldParent(oldParent), m_newParent(newParent),
              m_before(before), m_after(after), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        EntityId    m_child;
        EntityId    m_oldParent;
        EntityId    m_newParent;
        Transform   m_before;   ///< Local transform before the reparent (restored on undo).
        Transform   m_after;    ///< World-preserving local transform after (restored on redo).
        const char* m_label;
};

/**
 * @brief Make one camera the active ("main") camera, undoably.
 *
 * "Set as Main" / "Look Through" flip the active flag across every camera (one
 * on, the rest off) - a multi-entity change a single-entity ComponentEditCommand
 * can't capture. Records each camera's prior active flag (by slot) so undo
 * restores the exact previous selection.
 */
class SetActiveCameraCommand : public Command {
    public:
        SetActiveCameraCommand(EntityId target,
                               std::vector<std::pair<uint32_t, bool>> before,
                               const char* label)
            : m_target(target), m_before(std::move(before)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        EntityId m_target;
        std::vector<std::pair<uint32_t, bool>> m_before;  ///< (slotIndex, wasActive) per camera
        const char* m_label;
};

/**
 * @brief Rename a named asset (material or mesh), undoable.
 *
 * Records the asset handle plus its before/after names; redo/undo call
 * ResourceManager::rename (which keeps the per-type findByName index in sync).
 * Reaches the manager through a pointer captured at construction - there is no
 * Engine singleton, and the command stack is cleared on scene load, so the
 * pointer never outlives the manager it was taken from. An isAlive guard makes
 * the op a no-op if the asset was deleted after the rename (delete is not
 * itself undoable, so it can strand a rename on the stack).
 */
template <typename HandleType>
class RenameAssetCommand : public Command {
    public:
        RenameAssetCommand(ResourceManager& resources, HandleType handle,
                           std::string before, std::string after, const char* label)
            : m_resources(&resources), m_handle(handle),
              m_before(std::move(before)), m_after(std::move(after)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }

    private:
        ResourceManager* m_resources;
        HandleType       m_handle;
        std::string      m_before;
        std::string      m_after;
        const char*      m_label;
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

extern template class AddComponentCommand<Rigidbody>;
extern template class RemoveComponentCommand<Rigidbody>;
extern template class ComponentEditCommand<Rigidbody>;

extern template class AddComponentCommand<Collider>;
extern template class RemoveComponentCommand<Collider>;
extern template class ComponentEditCommand<Collider>;

extern template class AddComponentCommand<ReflectionProbe>;
extern template class RemoveComponentCommand<ReflectionProbe>;
extern template class ComponentEditCommand<ReflectionProbe>;

extern template class AddComponentCommand<Decal>;
extern template class RemoveComponentCommand<Decal>;
extern template class ComponentEditCommand<Decal>;

extern template class AddComponentCommand<ParticleEmitter>;
extern template class RemoveComponentCommand<ParticleEmitter>;
extern template class ComponentEditCommand<ParticleEmitter>;

extern template class AddComponentCommand<IrradianceVolume>;
extern template class RemoveComponentCommand<IrradianceVolume>;
extern template class ComponentEditCommand<IrradianceVolume>;

extern template class ComponentEditCommand<Light>;
extern template class ComponentEditCommand<Camera>;
extern template class ComponentEditCommand<Mesh>;
extern template class ComponentEditCommand<Name>;
extern template class ComponentEditCommand<Animation>;

extern template class AddComponentCommand<UICanvas>;
extern template class AddComponentCommand<UIElement>;
extern template class AddComponentCommand<UIImage>;
extern template class AddComponentCommand<UIText>;
extern template class AddComponentCommand<UIButton>;
extern template class RemoveComponentCommand<UICanvas>;
extern template class RemoveComponentCommand<UIElement>;
extern template class RemoveComponentCommand<UIImage>;
extern template class RemoveComponentCommand<UIText>;
extern template class RemoveComponentCommand<UIButton>;
extern template class ComponentEditCommand<UICanvas>;
extern template class ComponentEditCommand<UIElement>;
extern template class ComponentEditCommand<UIImage>;
extern template class ComponentEditCommand<UIText>;
extern template class ComponentEditCommand<UIButton>;

extern template class RenameAssetCommand<MaterialHandle>;
extern template class RenameAssetCommand<MeshHandle>;

} // namespace Engine
