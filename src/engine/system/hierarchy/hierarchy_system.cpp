#include "system/hierarchy/hierarchy_system.h"

#include <glm/glm.hpp>

#include "l_assert.h"

#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/hierarchy_operations.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {

void HierarchySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("HierarchySystem");
    resolve(ctx.scene);
}

void HierarchySystem::resolve(Scene& scene) {
    PROFILE_SCOPE("Hierarchy/ResolveWorld");
    SparseSet<Hierarchy>*       hierarchies = scene.storage<Hierarchy>();
    SparseSet<WorldTransform>*  worlds      = scene.storage<WorldTransform>();
    const SparseSet<Transform>* transforms  = scene.storage<Transform>();
    if (!hierarchies || !worlds) return;

    // No WorldTransform, no descent: the parallel pass reads a parent's slot unchecked.
    const auto admit = [&](EntityId id, std::vector<EntityId>& into) {
        VKM_ASSERT(worlds->contains(id.slot()), "HierarchySystem::resolve: Hierarchy without WorldTransform");
        if (worlds->contains(id.slot())) into.push_back(id);
    };

    const uint32_t count = static_cast<uint32_t>(hierarchies->size());
    m_level.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (!hierarchies->dataAt(i).parent) admit(scene.entityAt(hierarchies->keyAt(i)), m_level);
    }

    for (uint32_t depth = 0; !m_level.empty(); ++depth) {
        if (depth >= HierarchyOperations::MAX_DEPTH) {
            HierarchyOperations::warnWalkBound("world-transform resolve", HierarchyOperations::MAX_DEPTH);
            return;
        }

        // Without a Transform the matrix is kept, and children compose onto it.
        parallelFor(m_level.size(), [&](size_t i) {
            const uint32_t slot = m_level[i].slot();
            if (!transforms || !transforms->contains(slot)) return;

            const glm::mat4 local  = Transform::computeModelMatrix(transforms->get(slot));
            const EntityId  parent = hierarchies->get(slot).parent;
            worlds->get(slot).model = parent ? worlds->get(parent.slot()).model * local : local;
        });

        // Admitted only from the parent it names, so crossed sibling lists add nothing
        // twice; forEachChild bounds a looping list.
        m_next.clear();
        for (const EntityId node : m_level) {
            HierarchyOperations::forEachChild(scene, node, [&](EntityId child) {
                if (hierarchies->contains(child.slot()) && hierarchies->get(child.slot()).parent == node) {
                    admit(child, m_next);
                }
            });
        }
        m_level.swap(m_next);
    }
}

} // namespace Vkm::Engine
