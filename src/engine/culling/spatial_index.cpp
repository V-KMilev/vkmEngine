#include "spatial_index.h"

#include <future>

#include "thread_pool.h"

#include "scene.h"
#include "mesh.h"
#include "transform.h"
#include "resource_manager.h"
#include "frustum_culler.h"

#include "thread_pool.h"

namespace Engine {

// TODO: Make this configurable or think of better way
constexpr size_t PARALLEL_THRESHOLD = 500;

SpatialIndex::SpatialIndex() {
    // BVH doesn't need initialization - it's built on first update
}

void SpatialIndex::update(const Scene& scene, const ResourceManager& resources) {
    m_stats.wasRebuilt = false;

    const auto& meshStorage = scene.storage<Mesh>();

    // Check if entity count changed (use count() for active components)
    size_t currentCount = meshStorage.count();
    if (currentCount != m_lastEntityCount) {
        m_needsRebuild = true;
        m_lastEntityCount = currentCount;
    }

    if (m_needsRebuild) {
        computeWorldBounds(scene, resources);
        rebuildBVH();
        m_needsRebuild = false;
        m_stats.wasRebuilt = true;
        ++m_stats.rebuildCount;
    }

    m_stats.entityCount = m_primitives.size();
}

void SpatialIndex::computeWorldBounds(const Scene& scene, const ResourceManager& resources) {
    const auto& meshStorage = scene.storage<Mesh>();
    const auto& transformStorage = scene.storage<Transform>();

    // Use dense entity list - only iterate entities that have Mesh component
    const auto& meshEntities = meshStorage.entities();
    const size_t entityCount = meshEntities.size();

    m_primitives.clear();
    m_primitives.reserve(entityCount);

    if (entityCount < PARALLEL_THRESHOLD) {
        for (size_t i = 0; i < entityCount; ++i) {
            EntityId id = meshEntities[i];

            const auto& mesh = meshStorage.get(id);
            if (!mesh.visible) continue;
            if (!transformStorage.has(id)) continue;

            const auto& transform = transformStorage.get(id);
            const auto& meshAsset = resources.get(mesh.mesh);
            if (meshAsset.boundsMin == meshAsset.boundsMax) continue;

            const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);
            glm::vec3 worldMin, worldMax;
            FrustumCuller::transformAABB(modelMatrix, meshAsset.boundsMin, meshAsset.boundsMax, worldMin, worldMax);

            BVHPrimitive prim;
            prim.entityId = static_cast<uint32_t>(id);
            prim.bounds = AABB(worldMin, worldMax);
            prim.centroid = (worldMin + worldMax) * 0.5f;
            m_primitives.push_back(prim);
        }
        return;
    }

    // Parallel computation of world bounds using ThreadPool
    constexpr unsigned int MAX_CHUNKS = 8;
    const size_t chunkSize = (entityCount + MAX_CHUNKS - 1) / MAX_CHUNKS;
    const unsigned int numChunks = static_cast<unsigned int>((entityCount + chunkSize - 1) / chunkSize);

    std::vector<std::future<std::vector<BVHPrimitive>>> futures;
    futures.reserve(numChunks);

    for (unsigned int t = 0; t < numChunks; ++t) {
        const size_t startIdx = t * chunkSize;
        const size_t endIdx = std::min(startIdx + chunkSize, entityCount);

        futures.push_back(ThreadPool::get().push([&, startIdx, endIdx]() {
            std::vector<BVHPrimitive> localResults;
            localResults.reserve(endIdx - startIdx);

            for (size_t i = startIdx; i < endIdx; ++i) {
                EntityId id = meshEntities[i];

                const auto& mesh = meshStorage.get(id);
                if (!mesh.visible) continue;
                if (!transformStorage.has(id)) continue;

                const auto& transform = transformStorage.get(id);
                const auto& meshAsset = resources.get(mesh.mesh);
                if (meshAsset.boundsMin == meshAsset.boundsMax) continue;

                const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);
                glm::vec3 worldMin, worldMax;
                FrustumCuller::transformAABB(modelMatrix, meshAsset.boundsMin, meshAsset.boundsMax, worldMin, worldMax);

                BVHPrimitive prim;
                prim.entityId = static_cast<uint32_t>(id);
                prim.bounds = AABB(worldMin, worldMax);
                prim.centroid = (worldMin + worldMax) * 0.5f;
                localResults.push_back(prim);
            }
            return localResults;
        }));
    }

    // Collect results
    for (auto& future : futures) {
        auto results = future.get();
        m_primitives.insert(
            m_primitives.end(),
            std::make_move_iterator(results.begin()),
            std::make_move_iterator(results.end())
        );
    }
}

void SpatialIndex::rebuildBVH() {
    m_bvh.build(m_primitives);
}

std::vector<uint32_t> SpatialIndex::queryVisible(const Frustum& frustum) const {
    // Early-out: if scene bounds not visible, skip BVH traversal entirely
    if (!m_bvh.empty() && !FrustumCuller::isAABBVisible(frustum, m_bvh.getRootMin(), m_bvh.getRootMax())) {
        return {};
    }
    return m_bvh.queryFrustum(frustum);
}

} // namespace Engine
