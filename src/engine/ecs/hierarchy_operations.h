#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"

namespace Vkm::Engine::HierarchyOperations {

/**
 * @brief Attach a child entity to a parent entity, as its last child or in front of one of its children.
 *
 * A cycle-forming parent or a dead entity at either end is refused with a warning.
 *
 * @param scene  Scene both entities live in; Hierarchy and WorldTransform are
 *               added to whichever endpoint lacks them.
 * @param child  Entity to move, detached from wherever it is now.
 * @param parent Entity to attach it under; must not be a descendant of @p child.
 * @param before A child of @p parent to insert in front of; null, or anything
 *               that is not one of its children, appends.
 */
void setParent(Scene& scene, EntityId child, EntityId parent, EntityId before = {});

/**
 * @brief Detach an entity from its parent.
 *
 * A Hierarchy left with no parent or children is removed, with its WorldTransform.
 *
 * @param scene  Scene the entity lives in.
 * @param entity Entity to detach; nothing happens when it has no parent.
 */
void removeFromParent(Scene& scene, EntityId entity);

/**
 * @brief Compute the world-space model matrix for an entity, accounting for hierarchy.
 *
 * An ancestor without a Transform ends the chain (identity above). Bounded by MAX_DEPTH.
 *
 * @param scene Scene supplying the Transform of every entity on the chain.
 * @param entity Entity whose chain is composed.
 * @return Its world matrix.
 */
glm::mat4 computeWorldMatrix(const Scene& scene, EntityId entity);

/**
 * @brief Deepest chain a walk along the hierarchy's links follows.
 *
 * Public so a walk outside this file cuts a too-deep or cyclic chain where
 * forSelfAndAncestors does.
 */
constexpr uint32_t MAX_DEPTH = 32;

/**
 * @brief Report a walk over the hierarchy that ran into its bound, once for the process.
 *
 * Reaching a bound means a ring or a too-deep chain, which every later walk hits too.
 * Defined in vkm_core, so there is one report however many modules walk.
 *
 * @param walk  Which walk stopped, for the line.
 * @param bound The bound it reached.
 */
void warnWalkBound(const char* walk, size_t bound);

namespace detail {

/**
 * @brief A breadth-first search's queue, borrowed from one kept per thread.
 *
 * Borrowed by swap, so a nested search starts a fresh queue instead of pushing onto
 * the outer one. The thread_local lives in vkm_core: one in this header would tie
 * its destructor to every gameplay module, and glibc does not unmap a library while
 * a thread holding such a registration lives.
 */
class SearchQueue {
    public:
        SearchQueue();
        ~SearchQueue();

        SearchQueue(const SearchQueue& other) = delete;
        SearchQueue& operator=(const SearchQueue& other) = delete;

        SearchQueue(SearchQueue && other) = delete;
        SearchQueue& operator=(SearchQueue && other) = delete;

    public:
        std::vector<EntityId>& entries() { return m_queue; }

    private:
        std::vector<EntityId> m_queue;
};

} // namespace detail

/**
 * @brief Call @p fn on each direct child of @p parent, in sibling order.
 *
 * Bounded by the scene's entity count; a walk past it is a ring and stops with a report.
 *
 * @tparam Fn Callable taking the child's EntityId; it may detach or destroy
 *            that child.
 * @param scene  Scene holding the links.
 * @param parent Entity whose children are walked; one with no Hierarchy has none.
 * @param fn     Called once per child.
 */
template<typename Fn>
void forEachChild(const Scene& scene, EntityId parent, Fn&& fn) {
    const Hierarchy* node = scene.tryGet<Hierarchy>(parent);
    if (!node) return;

    const size_t bound = scene.entityCount();
    size_t       step  = 0;
    for (EntityId child = node->firstChild; child; ++step) {
        if (step >= bound) {
            warnWalkBound("sibling-list walk", bound);
            return;
        }
        // Read before fn, which may remove this child's Hierarchy.
        const Hierarchy* held = scene.tryGet<Hierarchy>(child);
        const EntityId   next = held ? held->nextSibling : EntityId{};
        fn(child);
        child = next;
    }
}

/**
 * @brief Call @p fn on @p entity and then on each of its ancestors, nearest first.
 *
 * Stops at an entity with no Hierarchy or parent, when @p fn returns false, or with a
 * report after MAX_DEPTH entities.
 *
 * @tparam Fn Callable taking EntityId and returning whether to keep climbing.
 * @param scene  Scene holding the links.
 * @param entity Entity to start from, visited first.
 * @param fn     Called with each entity; returns whether to keep climbing.
 */
template<typename Fn>
void forSelfAndAncestors(const Scene& scene, EntityId entity, Fn&& fn) {
    EntityId at = entity;
    for (uint32_t step = 0; at; ++step) {
        if (step >= MAX_DEPTH) {
            warnWalkBound("ancestor walk", MAX_DEPTH);
            return;
        }
        if (!fn(at)) return;
        const Hierarchy* node = scene.tryGet<Hierarchy>(at);
        if (!node) return;
        at = node->parent;
    }
}

/**
 * @brief The nearest entity at or below @p root that @p match accepts.
 *
 * Breadth first; bounded by the scene's entity count, as collectSubtree is.
 *
 * @tparam Match Callable taking EntityId and answering whether it is the one.
 * @param scene Scene holding the links.
 * @param root Entity to start from, inclusive.
 * @param match Asked of each entity.
 * @return The first entity it accepts, or an invalid id.
 */
template<typename Match>
EntityId findInSelfOrDescendantsIf(const Scene& scene, EntityId root, Match match) {
    if (!root) return {};
    if (match(root)) return root;

    detail::SearchQueue queue;
    std::vector<EntityId>& pending = queue.entries();
    pending.push_back(root);
    const size_t bound = scene.entityCount();
    for (size_t i = 0; i < pending.size(); ++i) {
        if (pending.size() > bound) {
            warnWalkBound("breadth-first search", bound);
            return {};
        }
        EntityId found{};
        forEachChild(scene, pending[i], [&](EntityId child) {
            if (!found && match(child)) found = child;
            pending.push_back(child);
        });
        if (found) return found;
    }
    return {};
}

/**
 * @brief The nearest entity at or below @p root carrying a T.
 *
 * Breadth first, so the nearest wins: a rig two deep, not one under a prop in a hand.
 *
 * @tparam T Component to look for.
 * @param scene Scene holding the links and the components.
 * @param root Entity to start from, inclusive.
 * @return The entity carrying it, or an invalid id.
 */
template<typename T>
EntityId findInSelfOrDescendants(const Scene& scene, EntityId root) {
    return findInSelfOrDescendantsIf(scene, root, [&](EntityId id) { return scene.has<T>(id); });
}

/**
 * @brief The nearest entity at or above @p leaf carrying a T.
 *
 * @tparam T Component to look for.
 * @param scene Scene holding the links and the components.
 * @param leaf Entity to start from, inclusive.
 * @return The entity carrying it, or an invalid id.
 */
template<typename T>
EntityId findInSelfOrAncestors(const Scene& scene, EntityId leaf) {
    EntityId found{};
    forSelfAndAncestors(scene, leaf, [&](EntityId at) {
        if (scene.has<T>(at)) found = at;
        return !found;
    });
    return found;
}

/**
 * @brief Whether @p ancestor lies on @p node's chain of parents.
 *
 * Bounded by the live entity count, not MAX_DEPTH: a deeper chain may exist, and an
 * answer cut short there would let a reparent close a ring through it.
 *
 * @param scene    Scene holding the links.
 * @param ancestor Entity looked for on the chain.
 * @param node Entity to walk up from, exclusive; the same id twice answers false.
 * @return Whether @p ancestor is a strict ancestor of @p node.
 */
inline bool isAncestorOf(const Scene& scene, EntityId ancestor, EntityId node) {
    if (!ancestor || !node || ancestor == node) return false;

    const Hierarchy* at = scene.tryGet<Hierarchy>(node);
    for (size_t step = 0; at && at->parent && step < scene.entityCount(); ++step) {
        if (at->parent == ancestor) return true;
        at = scene.tryGet<Hierarchy>(at->parent);
    }
    return false;
}

/**
 * @brief Destroy an entity and all its descendants, each before its parent.
 *
 * The set is collectSubtree's; a cyclic subtree is refused whole.
 *
 * @param scene Scene the subtree is destroyed from.
 * @param entity Root of the subtree; nothing happens when it is dead.
 */
void destroyHierarchy(Scene& scene, EntityId entity);

/**
 * @brief @p root and every descendant, parents before children.
 *
 * The one definition of a subtree's extent. Breadth first, so a parent exists when a
 * child's link is wired on load. Past the live entity count it returns nothing rather
 * than a partial set, which would save half a prefab and report success.
 *
 * @param scene Scene holding the links.
 * @param root  Entity to start from, included in the result.
 * @return The subtree, breadth first; empty when @p root is dead or the links
 *         under it form a cycle.
 */
std::vector<EntityId> collectSubtree(const Scene& scene, EntityId root);

} // namespace Vkm::Engine::HierarchyOperations
