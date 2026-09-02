#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "ecs/component/core/transform.h"
#include "ecs/environment.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/missing_assets.h"
#include "ecs/component/core/name.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "resource/asset/material_asset.h"

#include "framework/command.h"

namespace Vkm::Engine {

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
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

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
 * @brief Undo/redo for the scene's physics-world settings.
 *
 * Its own command rather than a case of the environment one: the physics world
 * is scene-global beside the Environment, not part of it, and an undo that
 * restored a whole Environment to change gravity would take the sky with it.
 */
class PhysicsSettingsEditCommand : public Command {
    public:
        PhysicsSettingsEditCommand(
            const PhysicsSettings& before,
            const PhysicsSettings& after,
            const char* label
        );

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        PhysicsSettings m_before;
        PhysicsSettings m_after;
        const char*     m_label;
};

/**
 * @brief A group of already-applied commands undone/redone as one step.
 *
 * Batch operations over a multi-selection (delete, duplicate, gizmo drags)
 * build one of these from their per-entity commands, and the whole batch is a
 * single entry in the history.
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
        bool addresses(uint32_t slotIndex) const override {
            for (const auto& c : m_commands) {
                if (c->addresses(slotIndex)) return true;
            }
            return false;
        }

    private:
        std::vector<std::unique_ptr<Command>> m_commands;
        const char* m_label;
};

/**
 * @brief Add a component of type T to an entity.
 *
 * Templated so each component type gets its own concrete command without
 * runtime type erasure.
 */
template <typename T>
class AddComponentCommand : public Command {
    public:
        AddComponentCommand(EntityId e, T value, const char* label)
            : m_entity(e), m_value(std::move(value)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

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
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

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
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

    private:
        EntityId    m_entity;
        T           m_before;
        T           m_after;
        const char* m_label;
};

/**
 * @brief Undoable edit of an entity's whole ScriptComponent, held as JSON.
 *
 * A behavior list is move-only, so a script edit has no pair of values for
 * ComponentEditCommand<T> to copy. Its serialized form copies fine, and it is
 * the same document EntitySnapshot::scriptJson already resurrects a deleted
 * entity's scripts from - so the component appearing, a behavior being
 * attached, a field being typed into, a row being removed and the card's x are
 * one command over two strings, with "the entity has no ScriptComponent"
 * spelled as an empty one.
 *
 * Coalesces with the next script edit on the same entity inside one gesture,
 * for the reason ComponentEditCommand does: a drag on a behavior's float field
 * pushes on every frame it changes.
 */
class ScriptEditCommand : public Command {
    public:
        ScriptEditCommand(EntityId e, std::string before, std::string after, const char* label)
            : m_entity(e), m_before(std::move(before)), m_after(std::move(after)), m_label(label) {}

        void redo(Scene& scene, EditorState& state) override;
        void undo(Scene& scene, EditorState& state) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

        /**
         * @brief The entity's ScriptComponent as JSON, or empty when it has none.
         *
         * The "before" of any script edit, and the "after" once it has been
         * applied. Exposed because the inspector applies script edits live -
         * the field widgets write into the behavior itself - so the panel is
         * what reads the two states around one.
         *
         * @param scene Scene holding the entity.
         * @param id Entity to read.
         * @return The serialized component, or an empty string when absent.
         */
        static std::string capture(const Scene& scene, EntityId id);

    private:
        /**
         * @brief Put @p json back on @p id, removing the component when empty.
         *
         * @param scene Scene holding the entity.
         * @param id Entity to write.
         * @param json A ScriptComponent document, or empty for no component.
         */
        static void restore(Scene& scene, EntityId id, const std::string& json);

    private:
        EntityId    m_entity;
        std::string m_before;
        std::string m_after;
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
 *
 * PrefabInstance and PrefabEntity are on the list because they are what makes a
 * resurrected instance an instance rather than the entities it expanded to:
 * without the marker the scene writes the subtree out inline and stops naming
 * the prefab, and without the uids every override addresses an entity that no
 * longer answers to its number.
 */
#define VKM_EDITOR_SNAPSHOT_COMPONENTS(X) \
    X(Transform,       transform)         \
    X(MissingAssets,   missingAssets)     \
    X(Mesh,            mesh)              \
    X(LOD,             lod)               \
    X(Light,           light)            \
    X(Camera,          camera)           \
    X(Animation,       animation)        \
    X(Animator,        animator)         \
    X(BoneSocket,      boneSocket)       \
    X(Name,            name)             \
    X(Rigidbody,        rigidbody)        \
    X(Collider,         collider)         \
    X(CharacterController, characterController) \
    X(Joint,            joint)            \
    X(Ragdoll,          ragdoll)          \
    X(ReflectionProbe,  reflectionProbe)  \
    X(IrradianceVolume, irradianceVolume) \
    X(Decal,            decal)            \
    X(ParticleEmitter,  particleEmitter)  \
    X(AudioSource,      audioSource)      \
    X(AudioListener,    audioListener)    \
    X(UICanvas,         uiCanvas)         \
    X(UIElement,        uiElement)        \
    X(UIImage,          uiImage)          \
    X(UIText,           uiText)           \
    X(UIButton,         uiButton)         \
    X(PrefabEntity,     prefabEntity)     \
    X(PrefabInstance,   prefabInstance)

/**
 * @brief The component types the inspector adds, removes and edits through
 * undoable commands, as one row per type.
 *
 * Drives both the extern-template block at the bottom of this header and the
 * explicit instantiations in editor_commands.cpp, so the two lists can never
 * drift - a component enters the editor's undoable-mutation vocabulary by
 * adding one row here.
 *
 * Deliberately not the snapshot list above: that one is what a destroyed
 * entity is rebuilt from, and carries Transform, Name, PrefabEntity and
 * PrefabInstance, none of which the inspector offers as add/remove/edit.
 */
#define VKM_EDITOR_COMMAND_COMPONENTS(X) \
    X(Mesh)                              \
    X(Light)                             \
    X(Camera)                            \
    X(Animation)                         \
    X(Animator)                          \
    X(BoneSocket)                        \
    X(Rigidbody)                         \
    X(Collider)                          \
    X(CharacterController)               \
    X(Joint)                             \
    X(Ragdoll)                           \
    X(ReflectionProbe)                   \
    X(Decal)                             \
    X(ParticleEmitter)                   \
    X(AudioSource)                       \
    X(AudioListener)                     \
    X(IrradianceVolume)                  \
    X(LOD)                               \
    X(UICanvas)                          \
    X(UIElement)                         \
    X(UIImage)                           \
    X(UIText)                            \
    X(UIButton)

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
        /**
         * @brief Re-create @p snap's entity, optionally back under a parent.
         *
         * EntitySnapshot is leaf-only by design - subtree wiring belongs to
         * SubtreeSnapshot. But a newly created UI element is parented to the
         * selected canvas before it is snapshotted, and a UI element with no
         * UICanvas ancestor is not laid out or drawn at all. Redo carries the
         * parent slot so it comes back attached instead of silently invisible.
         *
         * @param parentSlot Slot of the parent to restore, or 0 for a root.
         */
        CreateEntityCommand(EntitySnapshot snap, const char* label, uint32_t parentSlot = 0)
            : m_snap(std::move(snap)), m_label(label), m_parentSlot(parentSlot) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_snap.slotIndex == slotIndex; }

    private:
        EntitySnapshot m_snap;
        const char*    m_label;
        uint32_t       m_parentSlot = 0;
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
        bool addresses(uint32_t slotIndex) const override;

    private:
        SubtreeSnapshot m_snap;
        EntityId        m_priorSelection;
        const char*     m_label;
};

/**
 * @brief Swap one subtree for another, undoable.
 *
 * For an operation that rebuilds an entity and everything under it rather than
 * editing a field - building a ragdoll from a rig, and clearing one. Neither is
 * an add, a remove or an edit of a component, so none of the commands above
 * covers it: what changes is the shape of the subtree.
 *
 * Holds both states rather than replaying the operation, because the operation
 * reads assets and settings that may not be the same next time. Restoring the
 * subtree, in either direction, is what SubtreeSnapshot::apply already does -
 * including re-stamping the entity references the bones carry.
 */
class SubtreeReplaceCommand : public Command {
    public:
        SubtreeReplaceCommand(SubtreeSnapshot before, SubtreeSnapshot after, const char* label)
            : m_before(std::move(before)), m_after(std::move(after)), m_label(label) {}

        void redo(Scene& scene, EditorState& state) override { swapTo(scene, state, m_after); }
        void undo(Scene& scene, EditorState& state) override { swapTo(scene, state, m_before); }
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override;

    private:
        void swapTo(Scene& scene, EditorState& state, const SubtreeSnapshot& to);

    private:
        SubtreeSnapshot m_before;
        SubtreeSnapshot m_after;
        const char*     m_label;
};

/**
 * @brief Place an instance of a prefab into the scene, undoable.
 *
 * Redo rebuilds the instance from the prefab file rather than from a snapshot of
 * the entities the placement expanded to, because that is what a placement is:
 * the scene keeps a reference, a pose and the overrides against it, and building
 * the subtree from the file is what a scene load does with them.
 *
 * Reaches the ResourceManager through a pointer captured at construction: the
 * prefab resolves its assets by name on every rebuild, and the command stack is
 * cleared on scene load, so the pointer never outlives the manager it was taken
 * from.
 */
class PlacePrefabCommand : public Command {
    public:
        /**
         * @brief Record a placement that has already happened.
         *
         * @param resources Manager the rebuild resolves the prefab's assets against.
         * @param instance The instance as the scene stores it: which prefab, and
         *                 the overrides the rebuild reapplies to its subtree.
         * @param root Instance root; only its slot is kept, so redo can rebuild
         *             at the same index a later command may refer to.
         * @param at Pose the instance was placed at.
         * @param label History entry text.
         */
        PlacePrefabCommand(ResourceManager& resources, PrefabInstance instance, EntityId root,
                           const Transform& at, const char* label)
            : m_resources(&resources), m_instance(std::move(instance)), m_rootSlot(root.slot()),
              m_at(at), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_rootSlot == slotIndex; }

    private:
        ResourceManager* m_resources;
        PrefabInstance   m_instance;
        uint32_t         m_rootSlot;
        Transform        m_at;
        const char*      m_label;
};

/**
 * @brief Change what one instance overrides on one component, undoable.
 *
 * Takes the place of ComponentEditCommand for an entity inside a prefab
 * instance: the component's value there is the prefab's, patched by the
 * instance's overrides, so the edit that has to be reversed is the override
 * entry rather than the value. Restoring the entries and re-reading the
 * component from the file lands on exactly what a reload of the scene would
 * show, which a stored component value could disagree with.
 *
 * Entries are carried whole per (entity, component) rather than as a delta, so
 * the two directions are the same operation with different data - and so
 * coalescing a drag is just taking the newer set.
 *
 * Holds the ResourceManager by pointer for the same reason PlacePrefabCommand
 * does.
 */
class PrefabOverrideCommand : public Command {
    public:
        /**
         * @brief Record an override change that has already been applied.
         *
         * The entity is named by its prefab uid rather than by its own slot,
         * because the instance's entities are rebuilt - by a scene load, by the
         * redo of the placement - into whatever slots are free at the time, and
         * only the root's is pinned. The uid is the identity the override uses
         * everywhere else, and the one that survives the rebuild.
         *
         * @param resources Manager the re-read resolves the prefab's assets against.
         * @param root      Instance root carrying the override list.
         * @param targetUid Prefab uid of the entity the entries address.
         * @param component Component key, as SceneSerializer writes it.
         * @param before    The entries for that pair before the edit.
         * @param after     The entries for it now.
         * @param label     History entry text.
         */
        PrefabOverrideCommand(ResourceManager& resources, EntityId root, uint32_t targetUid,
                              std::string component, std::vector<PrefabOverride> before,
                              std::vector<PrefabOverride> after, const char* label)
            : m_resources(&resources), m_root(root), m_targetUid(targetUid),
              m_component(std::move(component)), m_before(std::move(before)),
              m_after(std::move(after)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_root.slot() == slotIndex; }

    private:
        /**
         * @brief Make @p entries the instance's entries for this pair, and warn
         * when the prefab can no longer answer for the component.
         *
         * @param scene Scene holding the instance.
         * @param state Receives the warning when the value could not be re-read.
         * @param entries The entry set this direction installs.
         */
        void step(Scene& scene, EditorState& state, const std::vector<PrefabOverride>& entries);

    private:
        ResourceManager*            m_resources;
        EntityId                    m_root;
        uint32_t                    m_targetUid;
        std::string                 m_component;
        std::vector<PrefabOverride> m_before;
        std::vector<PrefabOverride> m_after;
        const char*                 m_label;
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
        // Either end counts: the step re-links the child under one of the two
        // parents, so a step that could put an entity inside a subtree is as
        // outlived as one that moves the subtree's own entity.
        bool addresses(uint32_t slotIndex) const override {
            return m_child.slot() == slotIndex
                || m_oldParent.slot() == slotIndex
                || m_newParent.slot() == slotIndex;
        }

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
        bool addresses(uint32_t slotIndex) const override;

    private:
        EntityId m_target;
        std::vector<std::pair<uint32_t, bool>> m_before;  ///< (slotIndex, wasActive) per camera
        const char* m_label;
};

/**
 * @brief Undoable edit of a material's parameters (before -> after).
 *
 * Materials are scene assets: an edit here changes what every mesh using the
 * handle looks like and marks the scene unsaved, so it is authored work and
 * belongs in the history beside the component edits. Its own command rather
 * than a case of ComponentEditCommand because a material is not on an entity -
 * it lives in the ResourceManager, and putting the value back has to bump the
 * asset's version so the renderer and the previews re-read it.
 *
 * The asset's identity - name, uid, source descriptor - is deliberately not
 * part of what is restored. A material's name is renamed through its own
 * command and indexed by the manager, so an undo that carried the name back
 * would silently reverse a rename it was never asked about.
 *
 * Holds the ResourceManager by pointer for the same reason PlacePrefabCommand
 * does, and no-ops on a handle whose asset has since been deleted.
 */
class MaterialEditCommand : public Command {
    public:
        /**
         * @brief Record a material edit that has already been applied.
         *
         * @param resources Manager owning the asset.
         * @param handle    The material that was edited.
         * @param before    Its parameters before the edit.
         * @param after     Its parameters now.
         * @param label     History entry text.
         */
        MaterialEditCommand(ResourceManager& resources, MaterialHandle handle,
                            MaterialAsset before, MaterialAsset after, const char* label)
            : m_resources(&resources), m_handle(handle), m_before(std::move(before)),
              m_after(std::move(after)), m_label(label) {}

        void redo(Scene&, EditorState&) override;
        void undo(Scene&, EditorState&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        /**
         * @brief Put @p value's parameters back on the live asset and commit.
         *
         * @param state Editor state marked unsaved by the change.
         * @param value The parameter set this direction installs.
         */
        void step(EditorState& state, const MaterialAsset& value);

    private:
        ResourceManager* m_resources;
        MaterialHandle   m_handle;
        MaterialAsset    m_before;
        MaterialAsset    m_after;
        const char*      m_label;
};

/**
 * @brief Rename a named asset (material or mesh), undoable.
 *
 * Records the asset handle plus its before/after names; redo/undo call
 * ResourceManager::rename (which keeps the per-type findByName index in sync).
 * Holds the manager by pointer (there is no Engine singleton), safe for the
 * same reason PlacePrefabCommand's is. An isAlive guard makes the op a no-op if
 * the asset was deleted after the rename (delete is not itself undoable, so it
 * can strand a rename on the stack).
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
#define VKM_EDITOR_EXTERN_COMMAND(Type)                 \
    extern template class AddComponentCommand<Type>;    \
    extern template class RemoveComponentCommand<Type>; \
    extern template class ComponentEditCommand<Type>;
VKM_EDITOR_COMMAND_COMPONENTS(VKM_EDITOR_EXTERN_COMMAND)
#undef VKM_EDITOR_EXTERN_COMMAND

// Name is off the list because the editor never offers removing one: an entity
// without a Name falls back to its type label, so the menu entry would read as
// a rename to "Entity 12" rather than as a deletion.
extern template class AddComponentCommand<Name>;
extern template class ComponentEditCommand<Name>;

// Keyed by handle type rather than by component type, so not on the list. One
// per asset kind the Asset Browser lets an author rename.
extern template class RenameAssetCommand<MaterialHandle>;
extern template class RenameAssetCommand<MeshHandle>;
extern template class RenameAssetCommand<TextureHandle>;
extern template class RenameAssetCommand<AnimationClipHandle>;
extern template class RenameAssetCommand<AudioClipHandle>;

} // namespace Vkm::Engine
