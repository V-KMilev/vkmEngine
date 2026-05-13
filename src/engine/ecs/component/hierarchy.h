#pragma once

#include "ecs/entity.h"

namespace Engine {

class Scene;

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

    /// True if this entity's WorldTransform needs to be recomputed.
    /// Defaults true so newly-created hierarchical entities resolve on the
    /// first HierarchySystem tick. Cleared by HierarchyOperations::resolveWorldTransforms.
    /// Set by HierarchyOperations::markDirty (cascades to descendants).
    bool dirty = true;
};

/**
 * @brief Surgically detach an entity from the hierarchy tree, fixing all cross-entity links.
 *
 * Reparents children to the entity's parent (or makes them roots if none), then
 * unlinks the entity from its parent's child list. Leaves the entity's own Hierarchy
 * component in a disconnected (all-null) state but does NOT remove it.
 *
 * Used by Scene::destroyEntity to prevent dangling sibling/parent/child pointers in
 * surviving entities, and by HierarchyOperations::detachAndReparentChildren which
 * additionally cleans up the component itself.
 */
void detachFromHierarchy(Scene& scene, EntityId entity);

} // namespace Engine
