#include "ecs/hierarchy_utils.h"

#include "logger.h"

namespace Engine::HierarchyUtils {

void setParent(Scene& scene, EntityId child, EntityId parent) {
    VKM_ASSERT(scene.isAlive(child), "HierarchyUtils::setParent: child is dead");
    VKM_ASSERT(scene.isAlive(parent), "HierarchyUtils::setParent: parent is dead");
    VKM_ASSERT(child != parent, "HierarchyUtils::setParent: entity cannot parent itself");

    // Cycle detection: walk ancestors of new parent to ensure child is not already an ancestor
    {
        EntityId ancestor = parent;
        uint32_t depth = 0;
        while (ancestor && depth < 32) {
            if (ancestor == child) {
                LOG_WARNING("HierarchyUtils::setParent: cycle detected, ignoring");
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

    // Ensure both entities have Hierarchy components
    if (!scene.has<Hierarchy>(child)) {
        scene.add(Entity(child), Hierarchy{});
    }
    if (!scene.has<Hierarchy>(parent)) {
        scene.add(Entity(parent), Hierarchy{});
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
}

void removeFromParent(Scene& scene, EntityId entity) {
    if (!scene.has<Hierarchy>(entity)) return;

    auto& h = scene.get<Hierarchy>(entity);
    if (!h.parent) return;

    // Unlink from sibling chain
    if (h.prevSibling) {
        scene.get<Hierarchy>(h.prevSibling).nextSibling = h.nextSibling;
    } else {
        // Entity was the first child - update parent's firstChild
        scene.get<Hierarchy>(h.parent).firstChild = h.nextSibling;
    }

    if (h.nextSibling) {
        scene.get<Hierarchy>(h.nextSibling).prevSibling = h.prevSibling;
    }

    h.parent = {};
    h.prevSibling = {};
    h.nextSibling = {};

    // Remove Hierarchy component if no remaining relationships
    if (!h.firstChild) {
        scene.remove<Hierarchy>(Entity(entity));
    }
}

glm::mat4 computeWorldMatrix(const Scene& scene, EntityId entity) {
    // Collect parent chain (bottom-up) into a fixed-size stack array
    static constexpr uint32_t MAX_DEPTH = 32;
    EntityId chain[MAX_DEPTH];
    uint32_t depth = 0;

    EntityId current = entity;
    while (current && depth < MAX_DEPTH) {
        chain[depth++] = current;

        if (scene.has<Hierarchy>(current)) {
            current = scene.get<Hierarchy>(current).parent;
        } else {
            break;
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

    // Destroy the entity itself
    scene.destroyEntity(Entity(entity));
}

} // namespace Engine::HierarchyUtils
