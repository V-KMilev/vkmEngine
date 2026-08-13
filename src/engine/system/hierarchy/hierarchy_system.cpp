#include "system/hierarchy/hierarchy_system.h"

#include "debug/profiler.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

void HierarchySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("HierarchySystem");
    HierarchyOperations::resolveWorldTransforms(ctx.scene, m_buckets);
}

} // namespace Engine
