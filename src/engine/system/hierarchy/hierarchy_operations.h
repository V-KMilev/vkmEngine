#pragma once

#include <glm/glm.hpp>

#include "ecs/scene.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/transform.h"

namespace Engine::HierarchyOperations {

/**
 * @brief Attach a child entity to a parent entity.
 *
 * If the child already has a parent, it is detached first.
 * Hierarchy components are added automatically to both entities as needed.
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
 * @brief Resolve world transforms for every entity with a Hierarchy component.
 *
 * For each such entity, computes its world matrix via computeWorldMatrix and
 * writes it to a WorldTransform component (added on demand). This is the
 * per-frame work HierarchySystem runs, exposed here as a free function so
 * editors / loaders / bake passes can trigger it outside the frame loop.
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
