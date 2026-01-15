#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "entity.h"
#include "bvh.h"

class ThreadPool;

namespace Engine {
    class Scene;
    class ResourceManager;
    struct Frustum;
}

namespace Engine {

/**
 * @brief Spatial acceleration structure for scene culling.
 *
 * SpatialIndex maintains a BVH (Bounding Volume Hierarchy) over all mesh entities in the scene,
 * enabling efficient view frustum culling and fast spatial queries. Internally, it builds BVHPrimitives
 * for each visible mesh, transforms their bounds to world space, and rebuilds the BVH in parallel
 * when the scene changes. It supports querying visible entities given a frustum, used for render culling.
 *
 * Not thread-safe; intended for use on the main thread or with explicit external synchronization.
 */
class SpatialIndex {
    public:
        SpatialIndex();
        ~SpatialIndex() = default;

        SpatialIndex(const SpatialIndex& other) = delete;
        SpatialIndex& operator=(const SpatialIndex& other) = delete;

        SpatialIndex(SpatialIndex && other) = delete;
        SpatialIndex& operator=(SpatialIndex && other) = delete;

    public:
        /**
         * @brief Update the spatial index from the scene and its resources.
         *
         * Rebuilds the list of primitives and the BVH if the set of entities has changed.
         * This will transform mesh bounds to world space and parallelize primitive preparation.
         *
         * @param scene The current scene.
         * @param resources The resource manager for mesh lookups.
         */
        void update(const Scene& scene, const ResourceManager& resources);

        /**
         * @brief Get entities visible in the given frustum.
         *
         * Performs BVH traversal with the provided frustum and returns entity IDs of visible meshes.
         *
         * @param frustum The view frustum to test against.
         * @return List of entity IDs that are visible.
         */
        std::vector<EntityId> getVisible(const Frustum& frustum) const;

        /**
         * @brief Get a read-only reference to the BVH.
         * Used for debugging, testing, or visualization.
         */
        const BVH& getBVH() const { return m_bvh; }

    private:
        /**
         * @brief Scan all mesh entities, transform bounds to world space, and generate BVH primitives.
         *
         * @param scene The scene to build from.
         * @param resources The resource manager for mesh data.
         */
        void computeWorldBounds(const Scene& scene, const ResourceManager& resources);

        /**
         * @brief Rebuild the BVH using the current list of primitives.
         */
        void rebuildBVH();

    private:
        BVH m_bvh;
        std::vector<BVHPrimitive> m_primitives;

        size_t m_lastEntityCount;
};

} // namespace Engine

