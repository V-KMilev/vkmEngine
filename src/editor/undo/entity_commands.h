#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "io/scene/prefab.h"

#include "undo/command.h"
#include "undo/entity_snapshot.h"

namespace Vkm::Engine {

class Scene;
class CommandHost;
class ResourceManager;

/**
 * @brief Create an entity, undoable; redo re-creates it at its captured slot (Scene::createEntityAt),
 *        and a step after it that names the entity follows it there (CommandStack::noteReturn).
 */
class CreateEntityCommand : public Command {
    public:
        /**
         * @brief Record an entity already created, and the parent it was created under.
         *
         * Redo restores the parent: a UI element with no UICanvas ancestor is not laid out or drawn.
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
        EntityId       m_destroyed;  ///< The id undo destroyed, which redo's entity returns as.
};

/**
 * @brief Destroy a whole subtree, undoable; undo reuses its captured slots with new generations.
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
 * @brief Create a whole subtree as one step; redo reuses the slots undo freed, so later steps still find it.
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
 * @brief Swap one subtree for another, holding both: a replayed operation could read changed assets.
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
        void redo(Scene& scene, CommandHost& host) override { swap(scene, host, m_before, m_after); }
        void undo(Scene& scene, CommandHost& host) override { swap(scene, host, m_after, m_before); }
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override {
            return m_before.holds(slotIndex) || m_after.holds(slotIndex);
        }

    private:
        /// Replace the subtree @p from describes - the one in the scene - with @p to.
        void swap(Scene& scene, CommandHost& host, SubtreeSnapshot& from, SubtreeSnapshot& to);

    private:
        SubtreeSnapshot m_before;
        SubtreeSnapshot m_after;
        const char*     m_label;
};

/**
 * @brief Place an instance of a prefab, undoable; redo rebuilds it from the file into the slots it had,
 *        and a step after it that names one of its entities follows it there (CommandStack::noteReturn).
 *
 * The ResourceManager pointer is safe: the Engine destroys the manager after every system.
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
        ResourceManager*             m_resources;
        PrefabInstance               m_instance;
        uint32_t                     m_rootSlot;
        uint32_t                     m_parentSlot;
        Prefab::BuiltSlots           m_builtSlots;  ///< Where the last build put each entity.
        std::map<uint32_t, EntityId> m_destroyed;   ///< The ids undo destroyed, by uid, which redo's return as.
        Transform                    m_at;
        const char*                  m_label;
};

/**
 * @brief Re-parent an entity, undoable; either parent may be null, and its local Transform travels with it.
 */
class ReparentCommand : public Command {
    public:
        ReparentCommand(
            EntityId child,
            EntityId oldParent,
            EntityId oldNext,
            EntityId newParent,
            EntityId newNext,
            const Transform& before,
            const Transform& after,
            const char* label
        )
            : m_child(child)
            , m_oldParent(oldParent)
            , m_oldNext(oldNext)
            , m_newParent(newParent)
            , m_newNext(newNext)
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
        EntityId    m_oldNext;  ///< The sibling it stood before.
        EntityId    m_newParent;
        EntityId    m_newNext;  ///< The sibling it was put before, or null for last.
        Transform   m_before;   ///< Restored on undo.
        Transform   m_after;    ///< World-preserving; restored on redo.
        const char* m_label;
};

/**
 * @brief Put @p roots under a new entity named "Group", as one undo step.
 *
 * The group stands at the middle of their positions under their common parent, or at the root
 * when they have none, and each keeps its world pose. The caller refuses what a prefab owns.
 *
 * @param scene Scene holding the roots, none an ancestor of another.
 * @param host  Where the step is pushed and the dirty flag set.
 * @param roots What goes under the group, in its order.
 * @return The group, or null for no roots.
 */
EntityId pushGroup(Scene& scene, CommandHost& host, const std::vector<EntityId>& roots);

} // namespace Vkm::Engine
