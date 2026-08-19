#include "ecs/component/hierarchy.h"

#include "ecs/scene.h"

namespace Vkm::Engine {

void detachFromHierarchy(Scene& scene, EntityId entity) {
    if (!scene.has<Hierarchy>(entity)) return;

    // Only the entity itself is guaranteed alive here. Scene::destroyEntity, the
    // sole caller, unlinks one entity at a time, so a link left over from an
    // earlier partial tear-down can still name a freed slot. Reading a neighbour
    // is only safe when both isAlive and has<Hierarchy> hold.
    auto safeHierarchy = [&](EntityId id) -> Hierarchy* {
        if (!id || !scene.isAlive(id) || !scene.has<Hierarchy>(id)) return nullptr;
        return &scene.get<Hierarchy>(id);
    };

    Hierarchy& h = scene.get<Hierarchy>(entity);

    EntityId child = h.firstChild;
    while (child) {
        Hierarchy* childH = safeHierarchy(child);
        if (!childH) break;  // child gone; remaining siblings are unreachable from here
        EntityId nextChild = childH->nextSibling;

        childH->prevSibling = {};
        childH->nextSibling = {};
        childH->parent = h.parent;

        if (Hierarchy* parentH = safeHierarchy(h.parent)) {
            childH->nextSibling = parentH->firstChild;
            if (Hierarchy* oldHead = safeHierarchy(parentH->firstChild)) {
                oldHead->prevSibling = child;
            }
            parentH->firstChild = child;
        }

        child = nextChild;
    }
    h.firstChild = {};

    if (h.parent) {
        if (Hierarchy* prev = safeHierarchy(h.prevSibling)) {
            prev->nextSibling = h.nextSibling;
        } else if (Hierarchy* parentH = safeHierarchy(h.parent)) {
            parentH->firstChild = h.nextSibling;
        }
        if (Hierarchy* next = safeHierarchy(h.nextSibling)) {
            next->prevSibling = h.prevSibling;
        }
    }

    h.parent = {};
    h.prevSibling = {};
    h.nextSibling = {};
}

} // namespace Vkm::Engine
