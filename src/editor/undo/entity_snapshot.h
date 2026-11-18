#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "ecs/entity.h"
#include "ecs/component/scene/missing_assets.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "io/scene/scene_components.h"

namespace Vkm::Engine {

class CommandStack;
class Scene;
struct ScriptComponent;

/**
 * @brief Snapshot of every component on one entity, so an undo resurrects it whole.
 *
 * Every value row of the scene format, plus PrefabEntity and PrefabInstance, which keep a resurrected
 * instance an instance. Hierarchy is not stored; SubtreeSnapshot rewires it.
 */
struct EntitySnapshot {
#define VKM_SNAPSHOT_SLOT(Type, Key) std::optional<Type>,
    using Values = std::tuple<
        VKM_SCENE_VALUE_COMPONENTS(VKM_SNAPSHOT_SLOT, VKM_SNAPSHOT_SLOT, VKM_SNAPSHOT_SLOT)
        std::optional<MissingAssets>,
        std::optional<PrefabEntity>,
        std::optional<PrefabInstance>
    >;
#undef VKM_SNAPSHOT_SLOT

    uint32_t                   slotIndex = 0;
    Values                     values;
    std::optional<std::string> script;  ///< The ScriptComponent, as its JSON: it is move-only.

    template <typename T>
    std::optional<T>& get() { return std::get<std::optional<T>>(values); }
    template <typename T>
    const std::optional<T>& get() const { return std::get<std::optional<T>>(values); }

    static EntitySnapshot capture(const Scene& scene, EntityId id);
    void apply(Scene& scene, EntityId id) const;

    /**
     * @brief Whether @p id is still the entity this snapshot was taken of, by Name and prefab uid.
     *
     * The history finds entities by slot, trusting its LIFO order; a world rebuilt underneath breaks
     * that, and a step would destroy whatever stands in the slot.
     *
     * @param scene Scene holding the entity.
     * @param id Live entity in the snapshot's slot.
     * @return false when @p id differs from the capture in either.
     */
    bool describes(const Scene& scene, EntityId id) const;
};

/**
 * @brief Where a restored snapshot's entities land; reused slots keep a later step's slot address valid.
 */
enum class SnapshotSlots {
    Reuse,   ///< The captured slots; they must be free.
    Fresh,   ///< Newly allocated slots.
    Foreign  ///< Newly allocated, in another world: the root's parent and outside references are dropped.
};

/**
 * @brief Snapshot of a whole subtree; nodes are breadth first, as HierarchyOperations::collectSubtree walks
 *        them.
 */
struct SubtreeSnapshot {
    struct Node {
        EntitySnapshot snap;
        uint32_t       parentSlot = 0;  ///< 0 for the subtree root.
        EntityId       entity;          ///< The id a reference from outside the subtree names it by: as
                                        ///< captured, or as a step last destroyed it.
    };
    std::vector<Node> nodes;
    uint32_t          rootParentSlot = 0;  ///< The root's original parent; 0 when it is top-level.
    uint32_t          rootNextSlot   = 0;  ///< The sibling the root stood before; 0 when it was the last.

    static SubtreeSnapshot capture(const Scene& scene, EntityId root);

    /**
     * @brief Rebuild the subtree in its own slots (SnapshotSlots::Reuse), as an undo or redo does.
     *
     * Each entity is named again by every reference to its Node::entity: one in the scene, a Joint's
     * other body or a slider's knob, and one a later step puts back. A reference the subtree holds to
     * an entity not back yet keeps its id, for that entity's return to follow.
     *
     * @param scene   Scene the entities are created in.
     * @param history Notes each entity's return, and names what came back before as it came back.
     * @return The subtree's root, or a null id when nothing came back.
     */
    EntityId apply(Scene& scene, CommandStack& history) const;

    /**
     * @brief Rebuild the subtree in new slots, a copy of what it was taken of.
     *
     * @param scene Scene the entities are created in.
     * @param slots SnapshotSlots::Fresh or SnapshotSlots::Foreign.
     * @return The copy's root, or a null id when nothing came back.
     */
    EntityId apply(Scene& scene, SnapshotSlots slots) const;

    /**
     * @brief Rebuild several subtrees as one, so a reference from one to another names its copy.
     *
     * Each comes back in fresh slots (SnapshotSlots::Foreign), its root at the top level.
     *
     * @param scene Scene the entities are created in.
     * @param trees Subtrees that share no entity.
     * @return Each tree's root, in order; null for one that did not come back.
     */
    static std::vector<EntityId> applyTogether(Scene& scene, const std::vector<SubtreeSnapshot>& trees);

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
 * @brief Re-resolve a captured entity id through its slot.
 *
 * An undone delete resurrects the entity at its slot with a fresh generation, which a held id
 * would never match. The stack's LIFO order means the slot then holds the entity the step was made
 * against.
 *
 * @param scene    Scene the slot is looked up in.
 * @param captured The id as the command recorded it.
 * @return The entity now living in that slot, or an invalid id when none does.
 */
EntityId liveEntity(const Scene& scene, EntityId captured);

/**
 * @brief Point what @p id's components name at the id each entity last came back as
 *        (CommandStack::returnOf).
 *
 * For a step that writes a value back: it holds the ids from when it was made, and an entity it names
 * may have been destroyed and brought back since, which re-pointed only what the scene held then.
 *
 * @param scene   Scene holding the entity.
 * @param id      The entity the step wrote.
 * @param history Holds the returns.
 */
void followReturns(Scene& scene, EntityId id, const CommandStack& history);

/**
 * @brief The entity in @p slot if it is a child of @p parent, to insert before; none appends.
 *
 * @param scene  Scene the slot is looked up in.
 * @param parent The parent the sibling must stand under.
 * @param slot   The captured sibling's slot; 0 for none.
 * @return The sibling, or an invalid id.
 */
EntityId siblingUnder(const Scene& scene, EntityId parent, uint32_t slot);

/**
 * @brief Create an entity at @p slot, or nothing when the slot is live.
 *
 * createEntityAt asserts on a live slot, and a step reclaiming one cannot rule that out: the world
 * may have been rebuilt underneath the history.
 *
 * @param scene Scene the entity is created in.
 * @param slot  The captured slot.
 * @return The entity, or an invalid id when the slot is taken.
 */
EntityId reclaimSlot(Scene& scene, uint32_t slot);

/**
 * @brief A ScriptComponent as the JSON a snapshot or a script step holds: it is move-only.
 *
 * @param script The component.
 * @return Its document.
 */
std::string scriptJsonOf(const ScriptComponent& script);

/**
 * @brief Add the ScriptComponent @p json describes to @p id.
 *
 * Non-throwing: an exception would escape an undo mid ImGui frame with a scene half-rebuilt, so a
 * document that does not parse adds nothing.
 *
 * @param scene Scene holding the entity.
 * @param id    Entity to add to.
 * @param json  A ScriptComponent document.
 */
void addScriptFromJson(Scene& scene, EntityId id, const std::string& json);

} // namespace Vkm::Engine
