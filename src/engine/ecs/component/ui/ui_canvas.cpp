#include "ecs/component/ui/ui_canvas.h"

#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/hierarchy_operations.h"

namespace Vkm::Engine {

bool hasCanvasAncestor(const Scene& scene, EntityId id) {
    const Hierarchy* node = scene.tryGet<Hierarchy>(id);
    if (!node) return false;
    // From the parent, not from the entity: a canvas is not inside itself.
    return static_cast<bool>(HierarchyOperations::findInSelfOrAncestors<UICanvas>(scene, node->parent));
}

} // namespace Vkm::Engine
