#include "ecs/component/ui/ui_canvas.h"

#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

bool hasCanvasAncestor(const Scene& scene, EntityId id) {
    EntityId at = id;
    for (uint32_t depth = 0; depth < HierarchyOperations::MAX_DEPTH; ++depth) {
        if (!scene.has<Hierarchy>(at)) return false;
        const EntityId parent = scene.get<Hierarchy>(at).parent;
        if (!parent || !scene.isAlive(parent)) return false;
        if (scene.has<UICanvas>(parent)) return true;
        at = parent;
    }
    return false;
}

} // namespace Vkm::Engine
