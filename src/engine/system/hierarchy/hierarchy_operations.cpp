#define VKM_LOG_CATEGORY "HIERARCHY"

#include "system/hierarchy/hierarchy_operations.h"

#include "logger.h"

#include "ecs/component/world_transform.h"

namespace Engine::HierarchyOperations {

namespace {
// Maximum supported hierarchy depth; deeper chains are clamped/skipped.

// Add a default-constructed T to `id` only if it doesn't already have one.
template<typename T>
void ensure(Scene& scene, EntityId id) {
    if (!scene.has<T>(id)) scene.add(id, T{});
}
} // namespace

void markDirty(Scene& scene, EntityId entity) {
    if (!entity || !scene.isAlive(entity) || !scene.has<Hierarchy>(entity)) return;

    auto& h = scene.get<Hierarchy>(entity);
    if (h.dirty) return;  // Already dirty; descendants must already be too

    h.dirty = true;
    // Cascade to descendants. forEachChild snapshots the next sibling before
    // each call; markDirty's guard above makes revisiting a dead or
    // already-dirty node a harmless no-op, so corrupt links can't loop us.
    forEachChild(scene, entity, [&](EntityId child) { markDirty(scene, child); });
}

void setParent(Scene& scene, EntityId child, EntityId parent) {
    VKM_ASSERT(scene.isAlive(child), "HierarchyOperations::setParent: child is dead");
    VKM_ASSERT(scene.isAlive(parent), "HierarchyOperations::setParent: parent is dead");
    VKM_ASSERT(child != parent, "HierarchyOperations::setParent: entity cannot parent itself");

    // Cycle detection: walk ancestors of new parent to ensure child is not already an ancestor
    {
        EntityId ancestor = parent;
        uint32_t depth = 0;
        while (ancestor && depth < MAX_DEPTH) {
            if (ancestor == child) {
                LOG_WARNING("HierarchyOperations::setParent: cycle detected, ignoring");
                return;
            }
            if (scene.has<Hierarchy>(ancestor)) {
                ancestor = scene.get<Hierarchy>(ancestor).parent;
            } else {
                break;
            }
            ++depth;
        }
    }

    // Detach from current parent (if any)
    removeFromParent(scene, child);

    // Ensure both entities have Hierarchy + WorldTransform components.
    // Pre-seeding WorldTransform here keeps the per-frame resolve pass free of
    // structural mutation, which is the precondition for parallelising it.
    ensure<Hierarchy>(scene, child);
    ensure<WorldTransform>(scene, child);
    ensure<Hierarchy>(scene, parent);
    ensure<WorldTransform>(scene, parent);

    auto& childH = scene.get<Hierarchy>(child);
    auto& parentH = scene.get<Hierarchy>(parent);

    // Prepend child to parent's child list
    childH.parent = parent;
    childH.nextSibling = parentH.firstChild;
    childH.prevSibling = {};

    if (parentH.firstChild) {
        scene.get<Hierarchy>(parentH.firstChild).prevSibling = child;
    }
    parentH.firstChild = child;

    // Reparenting changes the child's world matrix (and all descendants').
    // markDirty short-circuits if already dirty, which is fine - descendants
    // were already dirty too in that case.
    markDirty(scene, child);
}

void removeFromParent(Scene& scene, EntityId entity) {
    if (!scene.has<Hierarchy>(entity)) return;

    auto& h = scene.get<Hierarchy>(entity);
    if (!h.parent) return;

    // Snapshot every field we'll need AFTER the same-function remove<> below
    // - that call swap-and-pops the SparseSet slot, invalidating `h`.
    const EntityId parent      = h.parent;
    const EntityId prevSibling = h.prevSibling;
    const EntityId nextSibling = h.nextSibling;
    const bool     isLeaf      = !h.firstChild;

    // Sibling/parent link fix-up. Each get<> is guarded against malformed
    // links - a dangling neighbour shouldn't crash the editor; warn loudly
    // so the upstream corruption can be found and fixed.
    if (prevSibling) {
        if (scene.isAlive(prevSibling) && scene.has<Hierarchy>(prevSibling)) {
            scene.get<Hierarchy>(prevSibling).nextSibling = nextSibling;
        } else {
            LOG_WARNING("RemoveFromParent: prevSibling %u of entity %u has no Hierarchy (link corruption)",
                prevSibling.index, entity.index);
        }
    } else if (scene.isAlive(parent) && scene.has<Hierarchy>(parent)) {
        scene.get<Hierarchy>(parent).firstChild = nextSibling;
    } else {
        LOG_WARNING("RemoveFromParent: parent %u of entity %u has no Hierarchy (link corruption)",
            parent.index, entity.index);
    }

    if (nextSibling) {
        if (scene.isAlive(nextSibling) && scene.has<Hierarchy>(nextSibling)) {
            scene.get<Hierarchy>(nextSibling).prevSibling = prevSibling;
        } else {
            LOG_WARNING("RemoveFromParent: nextSibling %u of entity %u has no Hierarchy (link corruption)",
                nextSibling.index, entity.index);
        }
    }

    h.parent = {};
    h.prevSibling = {};
    h.nextSibling = {};

    // Detaching changes this entity's world matrix (and all descendants').
    markDirty(scene, entity);

    // No children left -> drop the now-pointless Hierarchy + WorldTransform.
    // After this call `h` is dangling (swap-and-pop); no further reads of it.
    if (isLeaf) {
        scene.remove<Hierarchy>(entity);
        scene.remove<WorldTransform>(entity);
    }
}

glm::mat4 computeWorldMatrix(const Scene& scene, EntityId entity) {
    // Collect parent chain (bottom-up) into a fixed-size stack array
    EntityId chain[MAX_DEPTH];
    uint32_t depth = 0;

    EntityId current = entity;
    while (current && depth < MAX_DEPTH) {
        // A dangling parent (dead entity or one missing Transform) breaks
        // the chain at this point - fall through into a partial-root fold
        // rather than asserting on scene.get<Transform> below.
        if (!scene.isAlive(current) || !scene.has<Transform>(current)) break;
        chain[depth++] = current;

        if (scene.has<Hierarchy>(current)) {
            current = scene.get<Hierarchy>(current).parent;
        } else {
            break;
        }
    }

    if (current && depth >= MAX_DEPTH) {
        // A deeper-than-supported chain would silently snap to identity from
        // a partial root; surface it once so misimports are loud.
        static bool warned = false;
        if (!warned) {
            LOG_WARNING("ComputeWorldMatrix: hierarchy depth exceeds %u; deeper ancestors ignored",
                MAX_DEPTH);
            warned = true;
        }
    }

    // Multiply local matrices top-down (root first)
    glm::mat4 worldMatrix(1.0f);
    for (uint32_t i = depth; i > 0; --i) {
        const auto& transform = scene.get<Transform>(chain[i - 1]);
        worldMatrix = worldMatrix * Transform::computeModelMatrix(transform);
    }

    return worldMatrix;
}

void destroyHierarchy(Scene& scene, EntityId entity) {
    if (!scene.isAlive(entity)) return;

    // Destroy descendants depth-first. forEachChild snapshots the next sibling
    // before each call, so destroying the current child's subtree (which
    // unlinks it) can't strand the walk.
    forEachChild(scene, entity, [&](EntityId child) { destroyHierarchy(scene, child); });

    // Detach from parent before destruction
    removeFromParent(scene, entity);

    // Destroy the entity itself. Per-entity destroy hooks (e.g. script
    // onDestroy) fire from Scene::destroyEntity, so every destroy path is
    // covered without this op knowing about them.
    scene.destroyEntity(entity);
}

} // namespace Engine::HierarchyOperations
