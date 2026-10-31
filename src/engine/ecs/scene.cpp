#include "ecs/scene.h"

#include <glm/glm.hpp>

#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/hierarchy_operations.h"

namespace Vkm::Engine {

void detachFromHierarchy(Scene& scene, EntityId entity) {
    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    if (!node) return;
    // A leaf, as every entity destroyHierarchy takes is: no child to keep in place.
    if (!node->firstChild) {
        HierarchyOperations::removeFromParent(scene, entity);
        return;
    }

    const EntityId  parent   = scene.isAlive(node->parent) ? node->parent : EntityId{};
    const glm::mat4 toParent = parent
        ? glm::inverse(HierarchyOperations::computeWorldMatrix(scene, parent))
        : glm::mat4(1.0f);

    HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
        if (!scene.isAlive(child)) return;
        const bool placed     = scene.has<Transform>(child);
        const glm::mat4 world = placed
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
