#define VKM_LOG_CATEGORY "HIERARCHY"

#include "system/hierarchy/hierarchy_system.h"

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

#include "ecs/component/world_transform.h"

namespace Engine {

void HierarchySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("HierarchySystem");
    resolve(ctx.scene);
}

void HierarchySystem::resolve(Scene& scene) {
    PROFILE_SCOPE("Hierarchy/ResolveWorld");
    auto* hierarchyStorage = scene.storage<Hierarchy>();
    if (!hierarchyStorage) return;

    // Bucket dirty entities by their absolute depth from a root. Within a
    // single depth, entities are mutually independent (no parent-child links
    // between siblings or cousins) so a parallelFor over the bucket is safe.
    // Depths are processed in order, so a child at depth d+1 always observes
    // its parent's finalised WorldTransform.
    //
    // Invariant relied on here: every entity with Hierarchy also has
    // WorldTransform (pre-seeded by setParent) - no structural mutation
    // happens inside the parallel section.
    //
    // Cleared, not freed: the buckets live on the system so their capacity
    // survives the frame and the pass constructs nothing.
    for (auto& b : m_buckets) b.clear();

    const uint32_t count = static_cast<uint32_t>(hierarchyStorage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entityIdx = hierarchyStorage->keyAt(i);
        const EntityId id = scene.entityAt(entityIdx);

        if (!scene.has<Transform>(id)) continue;

        const auto& h = hierarchyStorage->dataAt(i);
        if (!h.dirty) continue;

        VKM_ASSERT(scene.has<WorldTransform>(id),
            "HierarchySystem::resolve: Hierarchy without WorldTransform");

        uint32_t depth = 0;
        EntityId current = h.parent;
        while (current && depth < HierarchyOperations::MAX_DEPTH) {
            if (!hierarchyStorage->contains(current.index)) break;
            current = hierarchyStorage->get(current.index).parent;
            ++depth;
        }
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

    // Depth-bucket invariant lets each child read its parent's already-final
    // WorldTransform instead of re-walking the ancestor chain. parent_world
    // * local is one matrix multiply per dirty entity vs. depth-many in
    // computeWorldMatrix - a real win on shallow-but-wide scenes.
    for (uint32_t d = 0; d < HierarchyOperations::MAX_DEPTH; ++d) {
        const auto& bucket = m_buckets[d];
        if (bucket.empty()) continue;

        if (d == 0) {
            parallelFor(bucket.size(), [&](size_t i) {
                const EntityId id = bucket[i];
                scene.get<WorldTransform>(id).model =
                    Transform::computeModelMatrix(scene.get<Transform>(id));
                scene.get<Hierarchy>(id).dirty = false;
            });
        } else {
            parallelFor(bucket.size(), [&](size_t i) {
                const EntityId id = bucket[i];
                const Hierarchy& h = scene.get<Hierarchy>(id);
                // h.parent is read by raw index without an isAlive guard here (unlike
                // computeWorldMatrix) deliberately: the bucketing pass above already
                // walked and validated this entity's full ancestor chain via
                // contains(), and setParent guarantees every parent has a
                // WorldTransform. Destroy/reparent mark descendants dirty and fix the
                // links, so a stale parent index never survives into this pass.
                const glm::mat4 parentWorld =
                    scene.get<WorldTransform>(h.parent).model;
                const glm::mat4 local =
                    Transform::computeModelMatrix(scene.get<Transform>(id));
                scene.get<WorldTransform>(id).model = parentWorld * local;
                scene.get<Hierarchy>(id).dirty = false;
            });
        }
    }
}

} // namespace Engine
