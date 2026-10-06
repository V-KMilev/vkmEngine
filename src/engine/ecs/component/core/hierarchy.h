#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Component representing parent-child relationships in an entity hierarchy.
 *
 * An intrusive doubly-linked sibling list, O(1) attach/detach. An entity with no
 * parent and no children has none. Sibling order is walk order, decided by
 * HierarchyOperations::setParent.
 */
struct Hierarchy {
    EntityId parent{};        ///< Null at a subtree root.
    EntityId firstChild{};    ///< Null at a leaf.
    EntityId lastChild{};     ///< For O(1) append; null at a leaf.
    EntityId nextSibling{};
    EntityId prevSibling{};
};

/**
 * @brief Detach an entity from the hierarchy tree, handing its children to its parent.
 *
 * Children take the entity's place among its siblings, in order (or become
 * roots), each keeping its world pose. A child left with no relatives loses its
 * Hierarchy and WorldTransform as removeFromParent leaves any leaf.
 *
 * Scene-internal, for Scene::destroyEntity; edit the graph through
 * HierarchyOperations instead.
 *
 * @param scene  Scene the entity lives in.
 * @param entity Entity being spliced out, still alive.
 */
void detachFromHierarchy(Scene& scene, EntityId entity);

} // namespace Vkm::Engine
