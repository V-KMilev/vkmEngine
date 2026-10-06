#define VKM_LOG_CATEGORY "HIERARCHY"

#include "ecs/hierarchy_operations.h"

#include "logger.h"

#include "ecs/component/core/world_transform.h"

namespace Vkm::Engine::HierarchyOperations {

namespace {

template<typename T>
void ensure(Scene& scene, EntityId id) {
    if (!scene.has<T>(id)) scene.add(id, T{});
}

} // namespace

void warnWalkBound(const char* walk, size_t bound) {
    static bool s_warned = false;
    if (s_warned) return;
    s_warned = true;
    LOG_WARNING(
        "The %s stopped at its bound of %zu - the hierarchy's links form a ring "
        "or a chain deeper than the walk follows, so walks through it stop short",
        walk,
        bound
    );
}

void setParent(Scene& scene, EntityId child, EntityId parent, EntityId before) {
    VKM_ASSERT(scene.isAlive(child), "HierarchyOperations::setParent: child is dead");
    VKM_ASSERT(scene.isAlive(parent), "HierarchyOperations::setParent: parent is dead");
    VKM_ASSERT(child != parent, "HierarchyOperations::setParent: entity cannot parent itself");

    // Guarded as well as asserted: a stale id would land a Hierarchy on a dead or reused slot.
    if (!scene.isAlive(child) || !scene.isAlive(parent)) {
        LOG_WARNING("HierarchyOperations::setParent: an entity is dead, ignoring");
        return;
    }
    if (child == parent || isAncestorOf(scene, child, parent)) {
        LOG_WARNING("HierarchyOperations::setParent: cycle detected, ignoring");
        return;
    }

    removeFromParent(scene, child);

    // Pre-seeded so HierarchySystem::resolve's parallel pass never mutates structure.
    ensure<Hierarchy>(scene, child);
    ensure<WorldTransform>(scene, child);
    ensure<Hierarchy>(scene, parent);
    ensure<WorldTransform>(scene, parent);

    auto& childH = scene.get<Hierarchy>(child);
    auto& parentH = scene.get<Hierarchy>(parent);
    childH.parent = parent;

    Hierarchy* next = before != child ? scene.tryGet<Hierarchy>(before) : nullptr;
    if (next && next->parent == parent) {
        childH.prevSibling = next->prevSibling;
        childH.nextSibling = before;
        if (next->prevSibling) {
            scene.get<Hierarchy>(next->prevSibling).nextSibling = child;
        } else {
            parentH.firstChild = child;
        }
        next->prevSibling = child;
        return;
    }

    childH.prevSibling = parentH.lastChild;
    childH.nextSibling = {};
    if (parentH.lastChild) {
        scene.get<Hierarchy>(parentH.lastChild).nextSibling = child;
    } else {
        parentH.firstChild = child;
    }
    parentH.lastChild = child;
}

void removeFromParent(Scene& scene, EntityId entity) {
    Hierarchy* held = scene.tryGet<Hierarchy>(entity);
    if (!held) return;

    Hierarchy& h = *held;
    if (!h.parent) return;

    const EntityId parent      = h.parent;
    const EntityId prevSibling = h.prevSibling;
    const EntityId nextSibling = h.nextSibling;
    const bool     isLeaf      = !h.firstChild;

    // Guarded against malformed links: warn about a dangling neighbour, do not crash.
    if (prevSibling) {
        if (Hierarchy* prev = scene.tryGet<Hierarchy>(prevSibling)) {
            prev->nextSibling = nextSibling;
        } else {
            LOG_WARNING(
                "RemoveFromParent: prevSibling %u of entity %u has no Hierarchy (link corruption)",
                prevSibling.slot(),
                entity.slot()
            );
        }
    } else if (Hierarchy* above = scene.tryGet<Hierarchy>(parent)) {
        above->firstChild = nextSibling;
    } else {
        LOG_WARNING(
            "RemoveFromParent: parent %u of entity %u has no Hierarchy (link corruption)",
            parent.slot(),
            entity.slot()
        );
    }

    if (nextSibling) {
        if (Hierarchy* next = scene.tryGet<Hierarchy>(nextSibling)) {
            next->prevSibling = prevSibling;
        } else {
            LOG_WARNING(
                "RemoveFromParent: nextSibling %u of entity %u has no Hierarchy (link corruption)",
                nextSibling.slot(),
                entity.slot()
            );
        }
    } else if (Hierarchy* above = scene.tryGet<Hierarchy>(parent)) {
        above->lastChild = prevSibling;
    }

    h.parent = {};
    h.prevSibling = {};
    h.nextSibling = {};

    if (isLeaf) {
        scene.remove<Hierarchy>(entity);
        scene.remove<WorldTransform>(entity);
    }
}

glm::mat4 computeWorldMatrix(const Scene& scene, EntityId entity) {
    EntityId chain[MAX_DEPTH];
    uint32_t depth = 0;

    forSelfAndAncestors(scene, entity, [&](EntityId at) {
        if (!scene.has<Transform>(at)) return false;
        chain[depth++] = at;
        return true;
    });

    // Root first, so the chain is folded top-down.
    glm::mat4 worldMatrix(1.0f);
    for (uint32_t i = depth; i > 0; --i) {
        const auto& transform = scene.get<Transform>(chain[i - 1]);
        worldMatrix = worldMatrix * Transform::computeModelMatrix(transform);
    }

    return worldMatrix;
}

void destroyHierarchy(Scene& scene, EntityId entity) {
    // Reversed breadth first is deepest first, so each goes as a leaf. Destroy hooks may
    // take other subtree entities with them, hence the liveness test.
    const std::vector<EntityId> subtree = collectSubtree(scene, entity);
    for (auto it = subtree.rbegin(); it != subtree.rend(); ++it) {
        if (scene.isAlive(*it)) scene.destroyEntity(*it);
    }
}

namespace {

thread_local std::vector<EntityId> t_searchQueue;

} // namespace

detail::SearchQueue::SearchQueue() {
    m_queue.swap(t_searchQueue);
}

detail::SearchQueue::~SearchQueue() {
    m_queue.clear();
    t_searchQueue.swap(m_queue);
}

std::vector<EntityId> collectSubtree(const Scene& scene, EntityId root) {
    if (!scene.isAlive(root)) return {};

    std::vector<EntityId> out{root};
    for (size_t i = 0; i < out.size(); ++i) {
        if (out.size() > scene.entityCount()) {
            LOG_ERROR("Subtree under slot %u revisits itself; refusing to walk it", root.slot());
            return {};
        }
        forEachChild(scene, out[i], [&](EntityId child) {
            if (scene.isAlive(child)) out.push_back(child);
        });
    }
    return out;
}

} // namespace Vkm::Engine::HierarchyOperations

namespace Vkm::Engine {

void detachFromHierarchy(Scene& scene, EntityId entity) {
    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    if (!node) return;

    const EntityId  parent   = scene.isAlive(node->parent) ? node->parent : EntityId{};
    const glm::mat4 toParent = parent
        ? glm::inverse(HierarchyOperations::computeWorldMatrix(scene, parent))
        : glm::mat4(1.0f);

    HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
        if (!scene.isAlive(child)) return;
        const bool      placed = scene.has<Transform>(child);
        const glm::mat4 world  = placed
            ? HierarchyOperations::computeWorldMatrix(scene, child)
            : glm::mat4(1.0f);
        if (parent) {
            // Before the entity going, so its children take its place among its siblings.
            HierarchyOperations::setParent(scene, child, parent, entity);
        } else {
            HierarchyOperations::removeFromParent(scene, child);
        }
        if (placed) scene.get<Transform>(child) = Transform::fromModelMatrix(toParent * world);
    });

    HierarchyOperations::removeFromParent(scene, entity);
}

} // namespace Vkm::Engine
