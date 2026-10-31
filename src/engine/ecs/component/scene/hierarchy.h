#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Component representing parent-child relationships in an entity hierarchy.
 *
 * An intrusive doubly-linked sibling list, O(1) attach/detach. An entity with no
 * parent and no children has none. Sibling order is walk order, decided by
 * HierarchyOperations::setParent: the last parented is walked last, listed last in the editor and,
 * on a UI canvas, drawn on top. Saving and prefabs keep the order.
 */
struct Hierarchy {
    EntityId parent{};       ///< Null at a subtree root.
    EntityId firstChild{};   ///< Null at a leaf.
    EntityId lastChild{};    ///< For O(1) append; null at a leaf.
    EntityId nextSibling{};
    EntityId prevSibling{};
};

} // namespace Vkm::Engine
