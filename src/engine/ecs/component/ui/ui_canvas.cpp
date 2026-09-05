#include "ecs/component/ui/ui_canvas.h"

#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

bool hasCanvasAncestor(const Scene& scene, EntityId id) {
    if (!scene.has<Hierarchy>(id)) return false;
    // From the parent, not from the entity: a canvas is not inside itself.
    return static_cast<bool>(HierarchyOperations::findInSelfOrAncestors<UICanvas>(
        scene, scene.get<Hierarchy>(id).parent));
}

} // namespace Vkm::Engine
