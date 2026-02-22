#pragma once

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief Component representing parent-child relationships in an entity hierarchy.
 *
 * Uses an intrusive doubly-linked sibling list for O(1) attach/detach.
 * Only entities that participate in a hierarchy need this component --
 * root entities without parents or children have no Hierarchy component.
 *
 * Tree structure:
 *   parent.firstChild -> child1 -> child1.nextSibling -> child2 -> ... -> null
 *                                 child2.prevSibling -> child1
 */
struct Hierarchy {
    EntityId parent{};        ///< Parent entity (null = root of subtree)
    EntityId firstChild{};    ///< Head of child linked list (null = leaf)
    EntityId nextSibling{};   ///< Next child of the same parent
    EntityId prevSibling{};   ///< Previous child of the same parent (for O(1) removal)
};

} // namespace Engine
