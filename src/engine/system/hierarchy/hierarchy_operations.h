#pragma once

#include <glm/glm.hpp>

#include "ecs/scene.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/transform.h"

namespace Engine::HierarchyOperations {

/**
 * @brief Attach a child entity to a parent entity.
 *
 * If the child already has a parent, it is detached first. Both Hierarchy
 * and WorldTransform are added automatically to either endpoint that's
 * missing them - pre-seeding WorldTransform here is what lets the per-frame
 * resolve pass stay free of structural mutation and parallelise by depth.
 *
 * @param scene The scene containing both entities.
 * @param child The entity to attach as a child.
 * @param parent The entity to become the parent.
 */
void setParent(Scene& scene, EntityId child, EntityId parent);

/**
 * @brief Detach an entity from its parent.
 *
 * Removes the entity from its parent's child list. If the entity's Hierarchy
 * component has no remaining relationships (no parent, no children), it is removed.
 *
 * @param scene The scene containing the entity.
 * @param entity The entity to detach.
 */
void removeFromParent(Scene& scene, EntityId entity);

/**
 * @brief Compute the world-space model matrix for an entity, accounting for hierarchy.
 *
 * Walks up the parent chain and multiplies local matrices top-down.
 * For entities without a parent Hierarchy, returns the local model matrix.
 * Maximum supported hierarchy depth is 32.
 *
 * @param scene The scene containing the entity.
 * @param entity The entity whose world matrix to compute.
 * @return The world-space model matrix.
 */
glm::mat4 computeWorldMatrix(const Scene& scene, EntityId entity);

/**
 * @brief Mark an entity's world transform as needing recomputation.
 *
 * Sets Hierarchy::dirty on the entity and propagates to every descendant.
 * No-op if the entity has no Hierarchy component. Call this after mutating
 * a Transform that participates in a hierarchy so the next HierarchySystem
 * tick picks up the change.
 *
 * Not thread-safe - call from the main thread or after any parallel writes.
 *
 * @param scene The scene containing the entity.
 * @param entity The entity whose subtree should be marked dirty.
 */
void markDirty(Scene& scene, EntityId entity);

/**
 * @brief Deepest ancestor chain the resolve pass will follow.
 *
 * Public because HierarchySystem sizes its per-depth scratch by it.
 */
constexpr uint32_t MAX_DEPTH = 32;

/**
 * @brief Iterate over all direct children of an entity.
 *
 * @param scene The scene containing the entity.
 * @param parent The parent entity.
 * @param fn Callable with signature void(EntityId child).
 */
template<typename Fn>
void forEachChild(const Scene& scene, EntityId parent, Fn&& fn) {
    if (!scene.has<Hierarchy>(parent)) return;

    EntityId child = scene.get<Hierarchy>(parent).firstChild;
    while (child) {
        // Snapshot next BEFORE fn so a callback that detaches or destroys
        // the current child (Unparent context-menu, Delete, etc.) doesn't
        // strand the iteration on a child whose Hierarchy was just removed.
        const EntityId next = (scene.isAlive(child) && scene.has<Hierarchy>(child))
            ? scene.get<Hierarchy>(child).nextSibling
            : EntityId{};
        fn(child);
        child = next;
    }
}

/**
 * @brief Recursively destroy an entity and all its descendants.
 *
 * Children are destroyed depth-first before the entity itself.
 *
 * @param scene The scene containing the entity.
 * @param entity The root entity to destroy.
 */
void destroyHierarchy(Scene& scene, EntityId entity);

} // namespace Engine::HierarchyOperations
