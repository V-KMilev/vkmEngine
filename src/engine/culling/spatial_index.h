#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "bvh.h"

class ThreadPool;

namespace Engine {
    class Scene;
    class ResourceManager;
    struct Frustum;
}

namespace Engine {

/**
 * @brief Manages spatial indexing for efficient frustum culling.
 *
 * Maintains an octree that is rebuilt when entities change significantly.
 * Tracks entity positions and rebuilds the tree when needed.
 */
class SpatialIndex {
public:
    SpatialIndex();
    ~SpatialIndex() = default;

    /**
     * @brief Update the spatial index from the scene.
     *
     * Rebuilds the BVH if entities have been added/removed.
     * This should be called once per frame before querying.
     *
     * @param scene The scene containing entities.
     * @param resources Resource manager for mesh bounds.
     */
    void update(const Scene& scene, const ResourceManager& resources);

    /**
     * @brief Query visible entity IDs using the octree.
     * @param frustum The view frustum.
     * @return Vector of entity IDs that are potentially visible.
     */
    std::vector<uint32_t> queryVisible(const Frustum& frustum) const;

    /**
     * @brief Check if the index needs rebuilding.
     */
    bool needsRebuild() const { return m_needsRebuild; }

    /**
     * @brief Force a full rebuild next update.
     */
    void invalidate() { m_needsRebuild = true; }

    /**
     * @brief Get statistics about the spatial index.
     */
    struct Stats {
        size_t entityCount = 0;
        size_t rebuildCount = 0;
        bool wasRebuilt = false;
    };
    const Stats& getStats() const { return m_stats; }

    /**
     * @brief Get the underlying BVH for visualization.
     */
    const BVH& getBVH() const { return m_bvh; }

private:
    /**
     * @brief Compute world bounds for all entities and build BVH primitives.
     */
    void computeWorldBounds(const Scene& scene, const ResourceManager& resources);

    /**
     * @brief Rebuild the BVH from scratch.
     */
    void rebuildBVH();

    BVH m_bvh;
    std::vector<BVHPrimitive> m_primitives;

    bool m_needsRebuild = true;
    size_t m_lastEntityCount = 0;

    Stats m_stats;
};

} // namespace Engine

