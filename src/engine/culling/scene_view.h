#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <algorithm>
#include <thread>

#include "scene.h"
#include "resource_manager.h"
#include "spatial_index.h"
#include "frustum_culler.h"
#include "thread_pool.h"
#include "camera.h"
#include "transform.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

/**
 * @brief Manages scene visibility culling using BVH spatial indexing.
 *
 * SceneView owns the spatial index and thread pool for culling operations.
 * It computes which entities are visible from the active camera each frame.
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
        std::vector<uint32_t> getVisibleEntities(
            const Scene& scene,
            const ResourceManager& resources
        );

        const SpatialIndex& getSpatialIndex() const { return m_spatialIndex; }

    private:
        glm::mat4 computeViewProjection(const Scene& scene) const;

    private:
        SpatialIndex m_spatialIndex;
};

} // namespace Engine