#include "system/hierarchy/hierarchy_system.h"

#include "ecs/scene.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

void HierarchySystem::update(FrameContext& ctx) {
    HierarchyOperations::resolveWorldTransforms(ctx.scene);
}

} // namespace Engine
