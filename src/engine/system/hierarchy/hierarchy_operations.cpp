#define VKM_LOG_CATEGORY "HIERARCHY"

#include "system/hierarchy/hierarchy_operations.h"

#include <array>
#include <vector>

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

#include "ecs/component/world_transform.h"
#include "system/script/behavior_system.h"

namespace Engine::HierarchyOperations {

void markDirty(Scene& scene, EntityId entity) {
    if (!entity || !scene.isAlive(entity) || !scene.has<Hierarchy>(entity)) return;

    auto& h = scene.get<Hierarchy>(entity);
    if (h.dirty) return;  // Already dirty; descendants must already be too

    h.dirty = true;
    EntityId child = h.firstChild;
    while (child) {
        // A dead-or-Hierarchyless child terminates the walk - the sibling
        // chain runs through that node's nextSibling, so we can't continue
        // past it. Corruption is already logged at its source (removeFromParent
        // / detachFromHierarchy); silently stopping here keeps the engine alive.
        if (!scene.isAlive(child) || !scene.has<Hierarchy>(child)) break;
        const EntityId next = scene.get<Hierarchy>(child).nextSibling;
        markDirty(scene, child);
        child = next;
    }
}

void setParent(Scene& scene, EntityId child, EntityId parent) {
    VKM_ASSERT(scene.isAlive(child), "HierarchyOperations::setParent: child is dead");
    VKM_ASSERT(scene.isAlive(parent), "HierarchyOperations::setParent: parent is dead");
    VKM_ASSERT(child != parent, "HierarchyOperations::setParent: entity cannot parent itself");

    // Cycle detection: walk ancestors of new parent to ensure child is not already an ancestor
    {
        EntityId ancestor = parent;
        uint32_t depth = 0;
        while (ancestor && depth < 32) {
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
    // Pre-seeding WorldTransform here keeps resolveWorldTransforms() free of
    // structural mutation, which is the precondition for parallelising it.
    if (!scene.has<Hierarchy>(child)) {
        scene.add(Entity(child), Hierarchy{});
    }
    if (!scene.has<WorldTransform>(child)) {
        scene.add(Entity(child), WorldTransform{});
    }
    if (!scene.has<Hierarchy>(parent)) {
        scene.add(Entity(parent), Hierarchy{});
    }
    if (!scene.has<WorldTransform>(parent)) {
        scene.add(Entity(parent), WorldTransform{});
    }

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
        scene.remove<Hierarchy>(Entity(entity));
        scene.remove<WorldTransform>(Entity(entity));
    }
}

glm::mat4 computeWorldMatrix(const Scene& scene, EntityId entity) {
    // Collect parent chain (bottom-up) into a fixed-size stack array
    static constexpr uint32_t MAX_DEPTH = 32;
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

void resolveWorldTransforms(Scene& scene) {
    PROFILE_SCOPE("Hierarchy/ResolveWorld");
    auto* hierarchyStorage = scene.storage<Hierarchy>();
    if (!hierarchyStorage) return;

    // Bucket dirty entities by their absolute depth from a root. Within a
    // single depth, entities are mutually independent (no parent-child links
    // between siblings or cousins) so a parallelFor over the bucket is safe.
    // Depths are processed in order, so a child at depth d+1 always observes
    // its parent's finalised WorldTransform.
    //
    // Invariant relied on here: every entity with Hierarchy also has
    // WorldTransform (pre-seeded by setParent) - no structural mutation
    // happens inside the parallel section.
    static constexpr uint32_t MAX_DEPTH = 32;
    // Persist the bucket array across frames (cleared, not freed) so we don't
    // construct 32 std::vectors and reallocate their storage every frame -
    // resolveWorldTransforms runs once per frame on the main thread, so a
    // function-static thread_local is safe and keeps the per-depth capacity.
    static thread_local std::array<std::vector<EntityId>, MAX_DEPTH> buckets;
    for (auto& b : buckets) b.clear();

    const uint32_t count = static_cast<uint32_t>(hierarchyStorage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entityIdx = hierarchyStorage->keyAt(i);
        const EntityId id{entityIdx, scene.generationOf(entityIdx)};

        if (!scene.has<Transform>(id)) continue;

        const auto& h = hierarchyStorage->dataAt(i);
        if (!h.dirty) continue;

        VKM_ASSERT(scene.has<WorldTransform>(id),
            "resolveWorldTransforms: Hierarchy without WorldTransform");

        uint32_t depth = 0;
        EntityId current = h.parent;
        while (current && depth < MAX_DEPTH) {
            if (!hierarchyStorage->contains(current.index)) break;
            current = hierarchyStorage->get(current.index).parent;
            ++depth;
        }
        if (depth >= MAX_DEPTH) {
            static bool warned = false;
            if (!warned) {
                LOG_WARNING("ResolveWorldTransforms: hierarchy depth exceeds %u; entity %u skipped (and any descendants)",
                    MAX_DEPTH, id.index);
                warned = true;
            }
            continue;
        }
        buckets[depth].push_back(id);
    }

    // Depth-bucket invariant lets each child read its parent's already-final
    // WorldTransform instead of re-walking the ancestor chain. parent_world
    // * local is one matrix multiply per dirty entity vs. depth-many in
    // computeWorldMatrix - a real win on shallow-but-wide scenes.
    for (uint32_t d = 0; d < MAX_DEPTH; ++d) {
        const auto& bucket = buckets[d];
        if (bucket.empty()) continue;

        if (d == 0) {
            parallelFor(bucket.size(), [&](size_t i) {
                const EntityId id = bucket[i];
                scene.get<WorldTransform>(id).model =
                    Transform::computeModelMatrix(scene.get<Transform>(id));
                scene.get<Hierarchy>(id).dirty = false;
            });
        } else {
            parallelFor(bucket.size(), [&](size_t i) {
                const EntityId id = bucket[i];
                const Hierarchy& h = scene.get<Hierarchy>(id);
                const glm::mat4 parentWorld =
                    scene.get<WorldTransform>(h.parent).model;
                const glm::mat4 local =
                    Transform::computeModelMatrix(scene.get<Transform>(id));
                scene.get<WorldTransform>(id).model = parentWorld * local;
                scene.get<Hierarchy>(id).dirty = false;
            });
        }
    }
}

void destroyHierarchy(Scene& scene, EntityId entity) {
    if (!scene.isAlive(entity)) return;

    // Recursively destroy children first (depth-first)
    if (scene.has<Hierarchy>(entity)) {
        EntityId child = scene.get<Hierarchy>(entity).firstChild;
        while (child) {
            // Save next sibling before destroying (destroying invalidates current node)
            EntityId next = scene.has<Hierarchy>(child)
                ? scene.get<Hierarchy>(child).nextSibling
                : EntityId{};
            destroyHierarchy(scene, child);
            child = next;
        }
    }

    // Detach from parent before destruction
    removeFromParent(scene, entity);

    // Fire onDestroy on any running behaviors before their ScriptComponent is
    // torn down (this is the chokepoint for entity + subtree deletion).
    BehaviorSystem::destroyEntityBehaviors(scene, entity);

    // Destroy the entity itself
    scene.destroyEntity(Entity(entity));
}

} // namespace Engine::HierarchyOperations
