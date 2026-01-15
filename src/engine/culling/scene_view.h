#pragma once

#include <vector>

#include "entity.h"
#include "spatial_index.h"

namespace Engine {
    class Scene;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Manages scene visibility culling using BVH spatial indexing.
 *
 * SceneView owns the spatial index and computes which entities are visible
 * from the active camera each frame using frustum culling.
 */
class SceneView {
    public:
        SceneView();
        ~SceneView() = default;

        SceneView(const SceneView& other) = delete;
        SceneView& operator=(const SceneView& other) = delete;

        SceneView(SceneView && other) = delete;
        SceneView& operator=(SceneView && other) = delete;

    public:
        /**
         * @brief Get all visible entities from the scene.
         * @param scene The scene containing entities.
         * @param resources The resource manager for accessing mesh data.
         * @return Sorted vector of visible entity IDs.
         */
        std::vector<EntityId> getVisibleEntities(
            const Scene& scene,
            const ResourceManager& resources
        );

        /**
         * @brief Get the spatial index for debugging/visualization.
         */
        const SpatialIndex& getSpatialIndex() const { return m_spatialIndex; }

    private:
        SpatialIndex m_spatialIndex;
};

} // namespace Engine
