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
 * missing them - pre-seeding WorldTransform here is what lets
 * resolveWorldTransforms() stay free of structural mutation and parallelise
 * by depth.
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
 * @brief Detach an entity from its hierarchy, reparenting its children to its parent.
 *
 * Children of @p entity become children of @p entity's parent (root if none),
 * then @p entity itself is unlinked. Used before destroying an entity when you
 * want to preserve its descendants in the tree. Does NOT destroy the entity.
 *
 * @param scene The scene containing the entity.
 * @param entity The entity to detach.
 */
void detachAndReparentChildren(Scene& scene, EntityId entity);

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
 * a Transform that participates in a hierarchy so the next
 * resolveWorldTransforms() picks up the change.
 *
 * Not thread-safe — call from the main thread or after any parallel writes.
 *
 * @param scene The scene containing the entity.
 * @param entity The entity whose subtree should be marked dirty.
 */
void markDirty(Scene& scene, EntityId entity);

/**
 * @brief Resolve world transforms for every dirty hierarchical entity.
 *
 * For each entity with a Hierarchy whose dirty flag is set, computes its
 * world matrix via computeWorldMatrix and writes it into the entity's
 * pre-seeded WorldTransform. Dirty entities are bucketed by absolute
 * depth in a serial pass and then each bucket runs through parallelFor;
 * parents finish before children by construction so reads of an
 * ancestor's matrix never race a write.
 *
 * This is the per-frame work HierarchySystem runs, exposed here as a
 * free function so editors / loaders / bake passes can trigger it
 * outside the frame loop.
 *
 * @param scene The scene to resolve.
 */
void resolveWorldTransforms(Scene& scene);

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
        fn(child);
        child = scene.get<Hierarchy>(child).nextSibling;
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
