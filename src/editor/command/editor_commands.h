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
#include "ecs/physics_settings.h"
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
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"
#include "io/scene/prefab.h"
#include "resource/asset/material_asset.h"

#include "command/command.h"

namespace Vkm::Engine {

class Scene;
class CommandHost;
class ResourceManager;

/**
 * @brief Undoable edit of a value the Scene holds outside any entity.
 *
 * Environment (sky, fog) and PhysicsSettings, which ComponentEditCommand cannot
 * reach. Coalesces consecutive edits of the same value, taking the newer label.
 *
 * @tparam T Environment or PhysicsSettings; instantiated at the bottom of this header.
 */
template <typename T>
class SceneValueEditCommand : public Command {
    public:
        SceneValueEditCommand(const T& before, const T& after, const char* label)
            : m_before(before)
            , m_after(after)
            , m_label(label)
        {}
        ~SceneValueEditCommand() override = default;

        SceneValueEditCommand(const SceneValueEditCommand& other) = delete;
        SceneValueEditCommand& operator=(const SceneValueEditCommand& other) = delete;

        SceneValueEditCommand(SceneValueEditCommand && other) = delete;
        SceneValueEditCommand& operator=(SceneValueEditCommand && other) = delete;

    public:
        void redo(Scene& scene, CommandHost&) override;
        void undo(Scene& scene, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        T           m_before;
        T           m_after;
        const char* m_label;
};

/**
 * @brief A group of already-applied commands undone/redone as one step.
 *
 * A composite with the same label pushed inside the same gesture is absorbed, so
 * a per-frame push stays one step per drag: each incoming step merges into the
 * held step that accepts it, else joins the end. That relies on no two steps of
 * a group editing the same component of the same entity; callers must keep it.
 */
class CompositeCommand : public Command {
    public:
        explicit CompositeCommand(const char* label) : m_label(label) {}
        ~CompositeCommand() override = default;

        CompositeCommand(const CompositeCommand& other) = delete;
        CompositeCommand& operator=(const CompositeCommand& other) = delete;

        CompositeCommand(CompositeCommand && other) = delete;
        CompositeCommand& operator=(CompositeCommand && other) = delete;

    public:
        /**
         * @brief Append an already-applied sub-command.
         *
         * @param cmd The step; redo replays in append order and undo in reverse.
         */
        void add(std::unique_ptr<Command> cmd) { m_commands.push_back(std::move(cmd)); }

        bool empty() const { return m_commands.empty(); }

        void redo(Scene& scene, CommandHost& host) override {
            for (auto& c : m_commands) c->redo(scene, host);
        }
        void undo(Scene& scene, CommandHost& host) override {
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
                (*it)->undo(scene, host);
        }
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
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
 */
template <typename T>
class AddComponentCommand : public Command {
    public:
        AddComponentCommand(EntityId e, T value, const char* label)
            : m_entity(e)
            , m_value(std::move(value))
            , m_label(label)
        {}
        ~AddComponentCommand() override = default;

        AddComponentCommand(const AddComponentCommand& other) = delete;
        AddComponentCommand& operator=(const AddComponentCommand& other) = delete;

        AddComponentCommand(AddComponentCommand && other) = delete;
        AddComponentCommand& operator=(AddComponentCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

    private:
        EntityId    m_entity;
        T           m_value;
        const char* m_label;
};

/**
 * @brief Remove a component of type T from an entity.
 */
template <typename T>
class RemoveComponentCommand : public Command {
    public:
        RemoveComponentCommand(EntityId e, T snapshot, const char* label)
            : m_entity(e)
            , m_snapshot(std::move(snapshot))
            , m_label(label)
        {}
        ~RemoveComponentCommand() override = default;

        RemoveComponentCommand(const RemoveComponentCommand& other) = delete;
        RemoveComponentCommand& operator=(const RemoveComponentCommand& other) = delete;

        RemoveComponentCommand(RemoveComponentCommand && other) = delete;
        RemoveComponentCommand& operator=(RemoveComponentCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
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
 * Only for components whose edit has no cross-entity or re-bake side effect.
 */
template <typename T>
class ComponentEditCommand : public Command {
    public:
        ComponentEditCommand(EntityId e, const T& before, const T& after, const char* label)
            : m_entity(e)
            , m_before(before)
            , m_after(after)
            , m_label(label)
        {}
        ~ComponentEditCommand() override = default;

        ComponentEditCommand(const ComponentEditCommand& other) = delete;
        ComponentEditCommand& operator=(const ComponentEditCommand& other) = delete;

        ComponentEditCommand(ComponentEditCommand && other) = delete;
        ComponentEditCommand& operator=(ComponentEditCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
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
 * A behavior list is move-only, so add, edit and remove are one command over two
 * strings, empty meaning no ScriptComponent. Coalesces with the next script edit
 * on the same entity inside one gesture.
 */
class ScriptEditCommand : public Command {
    public:
        ScriptEditCommand(EntityId e, std::string before, std::string after, const char* label)
            : m_entity(e)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~ScriptEditCommand() override = default;

        ScriptEditCommand(const ScriptEditCommand& other) = delete;
        ScriptEditCommand& operator=(const ScriptEditCommand& other) = delete;

        ScriptEditCommand(ScriptEditCommand && other) = delete;
        ScriptEditCommand& operator=(ScriptEditCommand && other) = delete;

    public:
        void redo(Scene& scene, CommandHost& host) override;
        void undo(Scene& scene, CommandHost& host) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

        /**
         * @brief The entity's ScriptComponent as JSON, or empty when it has none.
         *
         * @param scene Scene holding the entity.
         * @param id Entity to read.
         * @return The serialized component, or empty.
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
 * @brief Every component the editor knows how to put back, as (Type, field) rows.
 *
 * `X` rows also get the add, remove and edit commands; `S` rows only the snapshot.
 * Name is added and edited but never removed, so it is instantiated by hand at
 * the bottom of this header; MissingAssets nobody edits.
 * PrefabEntity and PrefabInstance keep a resurrected instance an instance:
 * without them the scene writes the subtree inline instead of naming the prefab,
 * and every override addresses an entity that no longer answers to its uid.
 * ScriptComponent is on neither: it is move-only and kept as JSON
 * (EntitySnapshot::scriptJson).
 */
#define VKM_EDITOR_COMPONENTS(X, S)             \
    X(Transform,           transform)           \
    S(MissingAssets,       missingAssets)       \
    X(Mesh,                mesh)                \
    X(LOD,                 lod)                 \
    X(Light,               light)               \
    X(Camera,              camera)              \
    X(Animation,           animation)           \
    X(Animator,            animator)            \
    X(BoneSocket,          boneSocket)          \
    S(Name,                name)                \
    X(Rigidbody,           rigidbody)           \
    X(Collider,            collider)            \
    X(CharacterController, characterController) \
    X(Joint,               joint)               \
    X(Ragdoll,             ragdoll)             \
    X(ReflectionProbe,     reflectionProbe)     \
    X(IrradianceVolume,    irradianceVolume)    \
    X(Decal,               decal)               \
    X(ParticleEmitter,     particleEmitter)     \
    X(AudioSource,         audioSource)         \
    X(AudioListener,       audioListener)       \
    X(UICanvas,            uiCanvas)            \
    X(UIElement,           uiElement)           \
    X(UIImage,             uiImage)             \
    X(UIText,              uiText)              \
    X(UIButton,            uiButton)            \
    X(UIScroll,            uiScroll)            \
    S(PrefabEntity,        prefabEntity)        \
    S(PrefabInstance,      prefabInstance)

/// Every row: the snapshot resurrects an entity whole.
#define VKM_EDITOR_SNAPSHOT_COMPONENTS(X) VKM_EDITOR_COMPONENTS(X, X)

/// The X rows alone, for the add/remove/edit commands.
#define VKM_EDITOR_SKIP_COMPONENT(Type, field)
#define VKM_EDITOR_COMMAND_COMPONENTS(X) VKM_EDITOR_COMPONENTS(X, VKM_EDITOR_SKIP_COMPONENT)

/**
 * @brief Snapshot of every editor-visible component on a single entity.
 *
 * Lets an undo resurrect an entity with the components it had. Hierarchy is not
 * stored; SubtreeSnapshot rewires it.
 */
struct EntitySnapshot {
    uint32_t slotIndex = 0;
#define VKM_SNAPSHOT_FIELD(Type, field) std::optional<Type> field;
    VKM_EDITOR_SNAPSHOT_COMPONENTS(VKM_SNAPSHOT_FIELD)
#undef VKM_SNAPSHOT_FIELD
    /**
     * @brief The entity's ScriptComponent, kept as its serialized JSON.
     *
     * ScriptComponent is move-only, so it cannot be stored as a value here.
     */
    std::optional<std::string>     scriptJson;

    static EntitySnapshot capture(const Scene& scene, EntityId id);
    void apply(Scene& scene, EntityId id) const;

    /**
     * @brief Whether @p id is still the entity this snapshot was taken of.
     *
     * The history reaches entities by slot and trusts its LIFO order to find the
     * right one there; a world rebuilt underneath it breaks that, and a step
     * destroying by slot would destroy whatever stands there. Answers from the
     * Name and prefab uid, which nothing changes without a step of its own.
     *
     * @param scene Scene holding the entity.
     * @param id Live entity in the snapshot's slot.
     * @return false when @p id differs from the capture in either.
     */
    bool describes(const Scene& scene, EntityId id) const;
};

/**
 * @brief Where the entities a restored snapshot lands in come from.
 *
 * Restoring what was destroyed reuses its slots, so a later step addressing it by
 * slot still finds it; a copy beside the original needs fresh ones.
 */
enum class SnapshotSlots {
    Reuse,  ///< The captured slots; they must be free.
    Fresh   ///< Newly allocated slots.
};

/**
 * @brief Snapshot of a whole subtree (entity + every descendant).
 *
 * Nodes are breadth first, as HierarchyOperations::collectSubtree walks them.
 */
struct SubtreeSnapshot {
    struct Node {
        EntitySnapshot snap;
        /**
         * @brief Slot of this node's parent, or 0 for the subtree root.
         */
        uint32_t parentSlot = 0;
    };
    std::vector<Node> nodes;
    /**
     * @brief Slot of the root's original parent, or 0 when it is top-level.
     */
    uint32_t rootParentSlot = 0;

    static SubtreeSnapshot capture(const Scene& scene, EntityId root);

    /**
     * @brief Rebuild the subtree in @p scene.
     *
     * @param scene Scene the entities are created in.
     * @param slots Where those entities come from.
     * @return The subtree's root, or a null id when nothing came back.
     */
    EntityId apply(Scene& scene, SnapshotSlots slots = SnapshotSlots::Reuse) const;

    /**
     * @brief Whether @p slotIndex is one of the entities this snapshot holds.
     *
     * @param slotIndex Entity slot the caller is asking about.
     * @return Whether a node was captured from that slot.
     */
    bool holds(uint32_t slotIndex) const;

    /**
     * @brief The live entity in the captured root's slot, if it is still that root.
     *
     * @param scene Scene to look in.
     * @return The root, or a null id when the snapshot is empty, the slot is free,
     *         or another entity holds it (EntitySnapshot::describes).
     */
    EntityId liveRoot(const Scene& scene) const;
};

/**
 * @brief Create a fresh entity with a fixed component set.
 *
 * Redo re-creates it at its captured slot (Scene::createEntityAt).
 */
class CreateEntityCommand : public Command {
    public:
        /**
         * @brief Record an entity already created, and the parent it was created under.
         *
         * Redo restores the parent: a UI element with no UICanvas ancestor is
         * not laid out or drawn.
         *
         * @param snap       The entity as created; its slot is the one redo reclaims.
         * @param label      History entry text.
         * @param parentSlot Slot of the parent to restore, or 0 for a root.
         */
        CreateEntityCommand(EntitySnapshot snap, const char* label, uint32_t parentSlot = 0)
            : m_snap(std::move(snap))
            , m_label(label)
            , m_parentSlot(parentSlot)
        {}
        ~CreateEntityCommand() override = default;

        CreateEntityCommand(const CreateEntityCommand& other) = delete;
        CreateEntityCommand& operator=(const CreateEntityCommand& other) = delete;

        CreateEntityCommand(CreateEntityCommand && other) = delete;
        CreateEntityCommand& operator=(CreateEntityCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
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
 * Undo reuses the captured slots (Scene::createEntityAt) with new generations.
 */
class DestroySubtreeCommand : public Command {
    public:
        DestroySubtreeCommand(SubtreeSnapshot snap, EntityId priorSelection, const char* label)
            : m_snap(std::move(snap))
            , m_priorSelection(priorSelection)
            , m_label(label)
        {}
        ~DestroySubtreeCommand() override = default;

        DestroySubtreeCommand(const DestroySubtreeCommand& other) = delete;
        DestroySubtreeCommand& operator=(const DestroySubtreeCommand& other) = delete;

        DestroySubtreeCommand(DestroySubtreeCommand && other) = delete;
        DestroySubtreeCommand& operator=(DestroySubtreeCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_snap.holds(slotIndex); }

    private:
        SubtreeSnapshot m_snap;
        EntityId        m_priorSelection;
        const char*     m_label;
};

/**
 * @brief Create a whole subtree (entity + every descendant), undoable.
 *
 * One command rather than a CreateEntityCommand per node, so an undo cannot take
 * half a subtree away. Redo puts it back into the slots undo freed, so a later
 * step addressing one of these entities by slot still finds it.
 */
class CreateSubtreeCommand : public Command {
    public:
        CreateSubtreeCommand(SubtreeSnapshot snap, const char* label)
            : m_snap(std::move(snap))
            , m_label(label)
        {}
        ~CreateSubtreeCommand() override = default;

        CreateSubtreeCommand(const CreateSubtreeCommand& other) = delete;
        CreateSubtreeCommand& operator=(const CreateSubtreeCommand& other) = delete;

        CreateSubtreeCommand(CreateSubtreeCommand && other) = delete;
        CreateSubtreeCommand& operator=(CreateSubtreeCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_snap.holds(slotIndex); }

    private:
        SubtreeSnapshot m_snap;
        const char*     m_label;
};

/**
 * @brief Swap one subtree for another, undoable.
 *
 * Holds both states rather than replaying the operation, which reads assets and
 * settings that may differ next time.
 */
class SubtreeReplaceCommand : public Command {
    public:
        SubtreeReplaceCommand(SubtreeSnapshot before, SubtreeSnapshot after, const char* label)
            : m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~SubtreeReplaceCommand() override = default;

        SubtreeReplaceCommand(const SubtreeReplaceCommand& other) = delete;
        SubtreeReplaceCommand& operator=(const SubtreeReplaceCommand& other) = delete;

        SubtreeReplaceCommand(SubtreeReplaceCommand && other) = delete;
        SubtreeReplaceCommand& operator=(SubtreeReplaceCommand && other) = delete;

    public:
        void redo(Scene& scene, CommandHost& host) override { swapTo(scene, host, m_after); }
        void undo(Scene& scene, CommandHost& host) override { swapTo(scene, host, m_before); }
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override {
            return m_before.holds(slotIndex) || m_after.holds(slotIndex);
        }

    private:
        void swapTo(Scene& scene, CommandHost& host, const SubtreeSnapshot& to);

    private:
        SubtreeSnapshot m_before;
        SubtreeSnapshot m_after;
        const char*     m_label;
};

/**
 * @brief Place an instance of a prefab into the scene, undoable.
 *
 * Redo rebuilds the instance from the prefab file, as a scene load does, rather
 * than from a snapshot. Undo records where the build put each entity, so redo
 * builds them back into those slots and a later step naming one by slot finds it.
 * The ResourceManager pointer is safe: the Engine destroys the manager after
 * every system, so it outlives the history.
 */
class PlacePrefabCommand : public Command {
    public:
        /**
         * @brief Record a placement that has already happened.
         *
         * @param resources Manager the rebuild resolves the prefab's assets against.
         * @param instance The prefab and the overrides the rebuild reapplies.
         * @param root Instance root; only its slot is kept, for redo to rebuild at.
         * @param at Pose the instance was placed at, local to @p parentSlot.
         * @param label History entry text.
         * @param parentSlot Slot of the instance's parent, or 0 for a root.
         */
        PlacePrefabCommand(
            ResourceManager& resources,
            PrefabInstance instance,
            EntityId root,
            const Transform& at,
            const char* label,
            uint32_t parentSlot = 0
        )
            : m_resources(&resources)
            , m_instance(std::move(instance))
            , m_rootSlot(root.slot())
            , m_parentSlot(parentSlot)
            , m_at(at)
            , m_label(label)
        {}
        ~PlacePrefabCommand() override = default;

        PlacePrefabCommand(const PlacePrefabCommand& other) = delete;
        PlacePrefabCommand& operator=(const PlacePrefabCommand& other) = delete;

        PlacePrefabCommand(PlacePrefabCommand && other) = delete;
        PlacePrefabCommand& operator=(PlacePrefabCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_rootSlot == slotIndex; }

    private:
        ResourceManager*   m_resources;
        PrefabInstance     m_instance;
        uint32_t           m_rootSlot;
        uint32_t           m_parentSlot;
        Prefab::BuiltSlots m_builtSlots;  ///< Where the last build put each entity.
        Transform          m_at;
        const char*        m_label;
};

/**
 * @brief Change what one instance overrides on one component, undoable.
 *
 * Replaces ComponentEditCommand inside a prefab instance: the value there is the
 * prefab's patched by the overrides, so the override entry is what is reversed.
 * Entries are carried whole per (entity, component), so both directions are the
 * same operation. Holds the ResourceManager by pointer as PlacePrefabCommand does.
 */
class PrefabOverrideCommand : public Command {
    public:
        /**
         * @brief Record an override change that has already been applied.
         *
         * The entity is named by prefab uid: only the instance root's slot is
         * pinned across a rebuild (Prefab::instantiateInto).
         *
         * @param resources Manager the re-read resolves the prefab's assets against.
         * @param root      Instance root carrying the override list.
         * @param targetUid Prefab uid of the entity the entries address.
         * @param component Component key, as SceneSerializer writes it.
         * @param before    Entries for that pair before the edit.
         * @param after     Entries for it now.
         * @param label     History entry text.
         */
        PrefabOverrideCommand(
            ResourceManager& resources,
            EntityId root,
            uint32_t targetUid,
            std::string component,
            std::vector<PrefabOverride> before,
            std::vector<PrefabOverride> after,
            const char* label
        )
            : m_resources(&resources)
            , m_root(root)
            , m_targetUid(targetUid)
            , m_component(std::move(component))
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~PrefabOverrideCommand() override = default;

        PrefabOverrideCommand(const PrefabOverrideCommand& other) = delete;
        PrefabOverrideCommand& operator=(const PrefabOverrideCommand& other) = delete;

        PrefabOverrideCommand(PrefabOverrideCommand && other) = delete;
        PrefabOverrideCommand& operator=(PrefabOverrideCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_root.slot() == slotIndex; }

    private:
        /**
         * @brief Install @p entries for this pair, warning when the prefab cannot answer.
         *
         * @param scene Scene holding the instance.
         * @param host  Receives the warning.
         * @param entries The entry set this direction installs.
         */
        void step(Scene& scene, CommandHost& host, const std::vector<PrefabOverride>& entries);

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
 * Either parent may be null for top-level. The child's local Transform travels
 * with the links, so a world-preserving reparent undoes with the matching one.
 */
class ReparentCommand : public Command {
    public:
        ReparentCommand(
            EntityId child,
            EntityId oldParent,
            EntityId newParent,
            const Transform& before,
            const Transform& after,
            const char* label
        )
            : m_child(child)
            , m_oldParent(oldParent)
            , m_newParent(newParent)
            , m_before(before)
            , m_after(after)
            , m_label(label)
        {}
        ~ReparentCommand() override = default;

        ReparentCommand(const ReparentCommand& other) = delete;
        ReparentCommand& operator=(const ReparentCommand& other) = delete;

        ReparentCommand(ReparentCommand && other) = delete;
        ReparentCommand& operator=(ReparentCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        // Either parent counts: a step that could put the child inside a subtree
        // is as outlived as one that moves the subtree's own entity.
        bool addresses(uint32_t slotIndex) const override {
            return m_child.slot() == slotIndex
                || m_oldParent.slot() == slotIndex
                || m_newParent.slot() == slotIndex;
        }

    private:
        EntityId    m_child;
        EntityId    m_oldParent;
        EntityId    m_newParent;
        Transform   m_before;   ///< Restored on undo.
        Transform   m_after;    ///< World-preserving; restored on redo.
        const char* m_label;
};

/**
 * @brief Make @p target the one active ("main") camera, as one undo step.
 *
 * Each camera whose flag moves is edited through editStep, so one inside a prefab
 * instance takes an override - the only way the scene keeps a value there.
 *
 * @param scene     Scene holding the cameras.
 * @param resources Resolves asset handles to names, for an override.
 * @param host      Where the step is pushed and the dirty flag set.
 * @param target    Camera made active.
 */
void pushActiveCamera(Scene& scene, ResourceManager& resources, CommandHost& host, EntityId target);

/**
 * @brief Undoable edit of a material's parameters (before -> after).
 *
 * A material lives in the ResourceManager, not on an entity, and putting a value
 * back must bump the asset's version so caches re-read it. Name, uid and source
 * descriptor are not restored: carrying the name back would silently reverse a
 * RenameAssetCommand. Holds the manager as PlacePrefabCommand does, and no-ops on
 * a handle whose asset has since been deleted.
 */
class MaterialEditCommand : public Command {
    public:
        /**
         * @brief Record a material edit that has already been applied.
         *
         * @param resources Manager owning the asset.
         * @param handle    Material edited.
         * @param before    Its parameters before the edit.
         * @param after     Its parameters now.
         * @param label     History entry text.
         */
        MaterialEditCommand(
            ResourceManager& resources,
            MaterialHandle handle,
            MaterialAsset before,
            MaterialAsset after,
            const char* label
        )
            : m_resources(&resources)
            , m_handle(handle)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~MaterialEditCommand() override = default;

        MaterialEditCommand(const MaterialEditCommand& other) = delete;
        MaterialEditCommand& operator=(const MaterialEditCommand& other) = delete;

        MaterialEditCommand(MaterialEditCommand && other) = delete;
        MaterialEditCommand& operator=(MaterialEditCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        /**
         * @brief Put @p value's parameters back on the live asset and commit.
         *
         * @param value The parameter set this direction installs.
         */
        void step(const MaterialAsset& value);

    private:
        ResourceManager* m_resources;
        MaterialHandle   m_handle;
        MaterialAsset    m_before;
        MaterialAsset    m_after;
        const char*      m_label;
};

/**
 * @brief Rename a named asset, undoable; instantiated at the bottom of this header.
 *
 * Holds the manager by pointer as PlacePrefabCommand does. No-ops on a deleted
 * asset: delete is not undoable, so it can strand a rename on the stack.
 */
template <typename HandleType>
class RenameAssetCommand : public Command {
    public:
        RenameAssetCommand(
            ResourceManager& resources,
            HandleType handle,
            std::string before,
            std::string after,
            const char* label
        )
            : m_resources(&resources)
            , m_handle(handle)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~RenameAssetCommand() override = default;

        RenameAssetCommand(const RenameAssetCommand& other) = delete;
        RenameAssetCommand& operator=(const RenameAssetCommand& other) = delete;

        RenameAssetCommand(RenameAssetCommand && other) = delete;
        RenameAssetCommand& operator=(RenameAssetCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }

    private:
        ResourceManager* m_resources;
        HandleType       m_handle;
        std::string      m_before;
        std::string      m_after;
        const char*      m_label;
};

// Instantiated in editor_commands.cpp, so includers need no bodies.
#define VKM_EDITOR_EXTERN_COMMAND(Type, field)          \
    extern template class AddComponentCommand<Type>;    \
    extern template class RemoveComponentCommand<Type>; \
    extern template class ComponentEditCommand<Type>;
VKM_EDITOR_COMMAND_COMPONENTS(VKM_EDITOR_EXTERN_COMMAND)
#undef VKM_EDITOR_EXTERN_COMMAND

// Name is never offered for removal: a nameless entity shows its type label, so
// removing it would read as a rename.
extern template class AddComponentCommand<Name>;
extern template class ComponentEditCommand<Name>;

extern template class SceneValueEditCommand<Environment>;
extern template class SceneValueEditCommand<PhysicsSettings>;

// Keyed by handle type, one per renamable asset kind.
extern template class RenameAssetCommand<MaterialHandle>;
extern template class RenameAssetCommand<MeshHandle>;
extern template class RenameAssetCommand<TextureHandle>;
extern template class RenameAssetCommand<AnimationClipHandle>;
extern template class RenameAssetCommand<AudioClipHandle>;

} // namespace Vkm::Engine
