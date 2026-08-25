#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine::HierarchyOperations {

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
 * @brief Deepest ancestor chain the resolve pass will follow.
 *
 * Public because HierarchySystem sizes its per-depth scratch by it.
 */
constexpr uint32_t MAX_DEPTH = 32;

/**
 * @brief Entities a downward search will visit before giving up.
 *
 * A runaway guard rather than a limit anyone should meet: a cycle makes a
 * breadth-first walk enqueue for as long as memory lasts, and a malformed
 * import is exactly when someone reaches for a search. Set far above any real
 * subtree - a rig is tens of bones, a level prop a handful - so a search that
 * hits it has found a loop rather than a large model.
 */
constexpr size_t MAX_SEARCH_NODES = 4096;

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
 * @brief The nearest entity at or below @p root carrying a T.
 *
 * A model import puts what a component needs on a different entity than the one
 * an author selects: the rig is on the node the bones are composed in, the
 * geometry on the nodes that draw, and the physics on the root because that is
 * where the Rigidbody has to be. Rather than making the author find the right
 * node, the code looks in the direction the answer is.
 *
 * Breadth first, so the nearest wins: a rig nested two deep is still the rig,
 * and a deeper one under a prop attached to a hand is not.
 *
 * @tparam T Component to look for.
 * @param scene Scene to walk.
 * @param root Entity to start from, inclusive.
 * @return The entity carrying it, or an invalid id.
 */
template<typename T>
EntityId findInSelfOrDescendants(const Scene& scene, EntityId root) {
    if (!root) return {};
    if (scene.has<T>(root)) return root;

    // Bounded like findInSelfOrAncestors below it. A cycle in the hierarchy
    // makes this queue grow for as long as memory lasts, and a malformed
    // import is exactly when someone reaches for a search.
    std::vector<EntityId> pending = { root };
    for (size_t i = 0; i < pending.size() && pending.size() < MAX_SEARCH_NODES; ++i) {
        EntityId found{};
        forEachChild(scene, pending[i], [&](EntityId child) {
            if (!found && scene.has<T>(child)) found = child;
            pending.push_back(child);
        });
        if (found) return found;
    }
    return {};
}

/**
 * @brief The nearest entity at or above @p leaf carrying a T.
 *
 * The other direction, for the same reason: a ragdoll is authored on the root
 * beside the Rigidbody, and the animation system reaches it from the rig node
 * underneath. Neither end has to know how deep the other is.
 *
 * @tparam T Component to look for.
 * @param scene Scene to walk.
 * @param leaf Entity to start from, inclusive.
 * @return The entity carrying it, or an invalid id.
 */
template<typename T>
EntityId findInSelfOrAncestors(const Scene& scene, EntityId leaf) {
    EntityId at = leaf;
    // Bounded by the same depth limit the resolve pass follows, so a hierarchy
    // that somehow formed a cycle stops rather than hanging the frame.
    for (uint32_t step = 0; at && step < MAX_DEPTH; ++step) {
        if (scene.has<T>(at)) return at;
        if (!scene.has<Hierarchy>(at)) return {};
        at = scene.get<Hierarchy>(at).parent;
    }
    return {};
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

} // namespace Vkm::Engine::HierarchyOperations
