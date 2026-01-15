#include "spatial_index.h"

#include <algorithm>
#include <future>
#include <optional>

#include "thread_pool.h"

#include "scene.h"
#include "mesh.h"
#include "transform.h"
#include "resource_manager.h"
#include "frustum_culler.h"

namespace Engine {

namespace {

    constexpr size_t MAX_CHUNKS = 8;

    /**
     * @brief Build a BVH primitive from an entity.
     * @return Optional primitive if entity is valid, empty otherwise.
     */
    std::optional<BVHPrimitive> buildPrimitive(
        EntityId id,
        const Scene& scene,
        const ResourceManager& resources
    ) {
        const auto& meshStorage = scene.storage<Mesh>();
        const auto& transformStorage = scene.storage<Transform>();

        if (!meshStorage.has(id) || !transformStorage.has(id)) {
            return std::nullopt;
        }

        const auto& mesh = meshStorage.get(id);
        if (!mesh.visible) {
            return std::nullopt;
        }

        const auto& transform = transformStorage.get(id);
        const auto& meshAsset = resources.get(mesh.mesh);

        // Skip invalid bounds
        if (meshAsset.boundsMin == meshAsset.boundsMax) {
            return std::nullopt;
        }

        const glm::mat4 modelMatrix = Transform::computeModelMatrix(transform);
        glm::vec3 worldMin, worldMax;
        FrustumCuller::transformAABB(modelMatrix, meshAsset.boundsMin, meshAsset.boundsMax, worldMin, worldMax);

        BVHPrimitive prim;
        prim.entityId = static_cast<uint32_t>(id);
        prim.bounds = AABB(worldMin, worldMax);
        prim.centroid = (worldMin + worldMax) * 0.5f;

        return prim;
    }

} // anonymous namespace

SpatialIndex::SpatialIndex()
    : m_bvh()
    , m_primitives()
    , m_lastEntityCount(0)
{}

void SpatialIndex::update(const Scene& scene, const ResourceManager& resources) {
    const auto& meshStorage = scene.storage<Mesh>();
    const size_t currentCount = meshStorage.size();

    // Rebuild if entity count changed
    if (currentCount != m_lastEntityCount) {
        computeWorldBounds(scene, resources);
        rebuildBVH();
        m_lastEntityCount = currentCount;
    }
}

void SpatialIndex::computeWorldBounds(const Scene& scene, const ResourceManager& resources) {
    const auto& meshStorage = scene.storage<Mesh>();
    const size_t entityCount = meshStorage.size();

    m_primitives.clear();
    m_primitives.reserve(entityCount);

    if (entityCount == 0) {
        return;
    }

    const size_t chunkSize = (entityCount + MAX_CHUNKS - 1) / MAX_CHUNKS;
    const size_t numChunks = (entityCount + chunkSize - 1) / chunkSize;

    std::vector<std::future<std::vector<BVHPrimitive>>> futures;
    futures.reserve(numChunks);

    // Submit work chunks to thread pool
    for (size_t chunk = 0; chunk < numChunks; ++chunk) {
        const size_t startIdx = chunk * chunkSize;
        const size_t endIdx = std::min(startIdx + chunkSize, entityCount);

        futures.push_back(ThreadPool::get().push([&, startIdx, endIdx]() {
            std::vector<BVHPrimitive> chunkResults;
            chunkResults.reserve(endIdx - startIdx);

            for (EntityId id = startIdx; id < endIdx; ++id) {
                if (auto primitive = buildPrimitive(id, scene, resources)) {
                    chunkResults.push_back(*primitive);
                }
            }

            return chunkResults;
        }));
    }

    // Collect results from all chunks
    for (auto& future : futures) {
        auto chunkResults = future.get();
        m_primitives.insert(
            m_primitives.end(),
            std::make_move_iterator(chunkResults.begin()),
            std::make_move_iterator(chunkResults.end())
        );
    }
}

void SpatialIndex::rebuildBVH() {
    m_bvh.build(std::move(m_primitives));
}

std::vector<EntityId> SpatialIndex::getVisible(const Frustum& frustum) const {
    // Early-out: if scene bounds not visible, skip BVH traversal entirely
    if (!m_bvh.empty() && !FrustumCuller::isAABBVisible(frustum, m_bvh.getRootMin(), m_bvh.getRootMax())) {
        return {};
    }

    return m_bvh.getVisibleInFrustum(frustum);
}

} // namespace Engine
