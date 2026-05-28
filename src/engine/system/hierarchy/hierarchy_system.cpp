#include "system/hierarchy/hierarchy_system.h"

#include "core/memory/types.h"
#include "debug/profiler.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "ecs/scene.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

void HierarchySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("HierarchySystem");
    HierarchyOperations::resolveWorldTransforms(ctx.scene);
}

} // namespace Engine
