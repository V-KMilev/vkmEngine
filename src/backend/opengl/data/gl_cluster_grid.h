#pragma once

#include <cstdint>
#include <memory>

#include "core/engine_config.h"

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

/**
 * @brief The Forward+ cluster light grid: one GPU buffer holding, per cluster,
 * a light count + up to MAX_LIGHTS_PER_CLUSTER light indices.
 *
 * GPU-only - the cull compute pass fills it each frame and the forward pass
 * reads it; the CPU never touches the contents. init() allocates it once the GL
 * context is live; bind() exposes it at the ClusterGrid SSBO binding point.
 */
class GLClusterGrid {
    public:
        static constexpr uint32_t NUM_CLUSTERS =
            Config::CLUSTER_X * Config::CLUSTER_Y * Config::CLUSTER_Z;

        // std430 ClusterLights: uint count + uint indices[MAX_LIGHTS_PER_CLUSTER],
        // tightly packed (all scalars), so this is the exact per-cluster stride.
        static constexpr uint32_t CLUSTER_STRIDE =
            (1u + Config::MAX_LIGHTS_PER_CLUSTER) * static_cast<uint32_t>(sizeof(uint32_t));

        GLClusterGrid();
        ~GLClusterGrid();

        GLClusterGrid(const GLClusterGrid& other) = delete;
        GLClusterGrid& operator=(const GLClusterGrid& other) = delete;

        GLClusterGrid(GLClusterGrid && other) = delete;
        GLClusterGrid& operator=(GLClusterGrid && other) = delete;

        /**
         * @brief Allocate the grid SSBO (a live GL context must exist). Idempotent.
         */
        void init();

        /**
         * @brief Bind the grid to the ClusterGrid SSBO binding point.
         */
        void bind() const;

    private:
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_ssbo;
};

} // namespace Vkm::Engine
