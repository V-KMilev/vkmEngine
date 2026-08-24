#define VKM_LOG_CATEGORY "HIERARCHY"

#include "system/hierarchy/hierarchy_system.h"

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

#include "ecs/component/core/world_transform.h"

namespace Vkm::Engine {

void HierarchySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("HierarchySystem");
    resolve(ctx.scene);
}

void HierarchySystem::resolve(Scene& scene) {
    PROFILE_SCOPE("Hierarchy/ResolveWorld");
    auto* hierarchyStorage = scene.storage<Hierarchy>();
    auto* worldStorage = scene.storage<WorldTransform>();
    if (!hierarchyStorage || !worldStorage) return;

    // Within a single depth, entities are mutually independent (no parent-child
    // links between siblings or cousins) and nothing below mutates the component
    // graph, so a parallelFor over a bucket is safe.
    for (auto& b : m_buckets) b.clear();

    const uint32_t count = static_cast<uint32_t>(hierarchyStorage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entityIdx = hierarchyStorage->keyAt(i);
        const EntityId id = scene.entityAt(entityIdx);

        if (!scene.has<Transform>(id)) continue;

        const auto& h = hierarchyStorage->dataAt(i);

        VKM_ASSERT(scene.has<WorldTransform>(id),
            "HierarchySystem::resolve: Hierarchy without WorldTransform");

        // Guarded along the whole chain, not just here: the depth loop below
        // reads the parent's matrix as well as its own, and in a build where
        // the assert is compiled out a missing slot is an out-of-range index.
        bool resolvable = worldStorage->contains(entityIdx);

        uint32_t depth = 0;
        EntityId current = h.parent;
        while (current && depth < HierarchyOperations::MAX_DEPTH) {
            if (!worldStorage->contains(current.index)) {
                resolvable = false;
                break;
            }
            if (!hierarchyStorage->contains(current.index)) break;
            current = hierarchyStorage->get(current.index).parent;
            ++depth;
        }
        if (!resolvable) continue;

        if (depth >= HierarchyOperations::MAX_DEPTH) {
            static bool warned = false;
            if (!warned) {
                LOG_WARNING("HierarchySystem::resolve: hierarchy depth exceeds %u; entity %u skipped (and any descendants)",
                    HierarchyOperations::MAX_DEPTH, id.index);
                warned = true;
            }
            continue;
        }
        m_buckets[depth].push_back(id);
    }

    // Depths in order: a child then reads a parent matrix that is already
    // final, one multiply instead of re-walking the chain per entity.
    for (uint32_t d = 0; d < HierarchyOperations::MAX_DEPTH; ++d) {
        const auto& bucket = m_buckets[d];
        if (bucket.empty()) continue;

        if (d == 0) {
            parallelFor(bucket.size(), [&](size_t i) {
                const EntityId id = bucket[i];
                scene.get<WorldTransform>(id).model =
                    Transform::computeModelMatrix(scene.get<Transform>(id));
            });
        } else {
            parallelFor(bucket.size(), [&](size_t i) {
                const EntityId id = bucket[i];
                const Hierarchy& h = scene.get<Hierarchy>(id);
                // Read by raw index without an isAlive guard, unlike
                // computeWorldMatrix: the bucketing pass validated this entity's
                // whole ancestor chain this same frame.
                const glm::mat4 parentWorld =
                    scene.get<WorldTransform>(h.parent).model;
                const glm::mat4 local =
                    Transform::computeModelMatrix(scene.get<Transform>(id));
                scene.get<WorldTransform>(id).model = parentWorld * local;
            });
        }
    }
}

} // namespace Vkm::Engine
