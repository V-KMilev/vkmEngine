#include "ecs/component/hierarchy.h"

#include "ecs/scene.h"

namespace Engine {

void detachFromHierarchy(Scene& scene, EntityId entity) {
    if (!scene.has<Hierarchy>(entity)) return;

    Hierarchy& h = scene.get<Hierarchy>(entity);

    EntityId child = h.firstChild;
    while (child) {
        Hierarchy& childH = scene.get<Hierarchy>(child);
        EntityId nextChild = childH.nextSibling;

        childH.prevSibling = {};
        childH.nextSibling = {};
        childH.parent = h.parent;

        if (h.parent) {
            Hierarchy& parentH = scene.get<Hierarchy>(h.parent);
            childH.nextSibling = parentH.firstChild;
            if (parentH.firstChild) {
                scene.get<Hierarchy>(parentH.firstChild).prevSibling = child;
            }
            parentH.firstChild = child;
        }

        child = nextChild;
    }
    h.firstChild = {};

    if (h.parent) {
        if (h.prevSibling) {
            scene.get<Hierarchy>(h.prevSibling).nextSibling = h.nextSibling;
        } else {
            scene.get<Hierarchy>(h.parent).firstChild = h.nextSibling;
        }
        if (h.nextSibling) {
            scene.get<Hierarchy>(h.nextSibling).prevSibling = h.prevSibling;
        }
    }

    h.parent = {};
    h.prevSibling = {};
    h.nextSibling = {};
}

} // namespace Engine
