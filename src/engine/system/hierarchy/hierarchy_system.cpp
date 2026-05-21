#include "system/hierarchy/hierarchy_system.h"

#include "core/memory/types.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/transform.h"
#include "ecs/component/world_transform.h"
#include "ecs/scene.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Engine {

SystemAccess HierarchySystem::declareAccess() const {
    return SystemAccess{
        /*reads*/  { typeId<Transform>(), typeId<Hierarchy>() },
        /*writes*/ { typeId<WorldTransform>() },
    };
}

void HierarchySystem::update(FrameContext& ctx) {
    HierarchyOperations::resolveWorldTransforms(ctx.scene);
}

} // namespace Engine
