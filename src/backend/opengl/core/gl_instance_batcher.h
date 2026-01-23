#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

#include "resource_manager.h"
#include "gl_instance_buffer.h"

namespace Engine {

/**
 * @brief Represents a single instance batch for rendering.
 *
 * Contains all data needed to issue a single instanced draw call:
 * the mesh to draw, material to bind, and the instance count.
 */
struct InstanceBatch {
    MeshHandle mesh;
    MaterialHandle material;
    uint32_t instanceCount = 0;
};

/**
 * @brief Builds and manages instance batches for efficient rendering.
 *
 * Takes sorted drawable data and groups consecutive drawables with the same
 * (mesh, material) combination into batches. Manages per-mesh instance buffers
 * and provides the data needed for instanced draw calls.
 *
 * This class is owned by GLView and operates per-frame.
 */
class GLInstanceBatcher {
public:
    GLInstanceBatcher() = default;
    ~GLInstanceBatcher() = default;

    GLInstanceBatcher(const GLInstanceBatcher&) = delete;
    GLInstanceBatcher& operator=(const GLInstanceBatcher&) = delete;
    GLInstanceBatcher(GLInstanceBatcher&&) = delete;
    GLInstanceBatcher& operator=(GLInstanceBatcher&&) = delete;

    /**
     * @brief Builds instance batches from sorted drawable data.
     *
     * Assumes drawables are already sorted by (material, mesh).
     * Groups consecutive drawables with identical keys into batches.
     * Updates instance buffers with model matrices.
     *
     * @param drawables Sorted drawable data from RenderView
     */
    void build(const std::vector<struct DrawableData>& drawables);

    /**
     * @brief Returns the built batches for rendering.
     */
    const std::vector<InstanceBatch>& getBatches() const { return m_batches; }

    /**
     * @brief Gets the instance buffer for a specific batch.
     *
     * @param batchIndex Index into the batches vector
     * @return Pointer to the instance buffer, or nullptr if not found
     */
    GLInstanceBuffer* getInstanceBuffer(size_t batchIndex);

    /**
     * @brief Clears all batches (called at frame start).
     */
    void clear();

private:
    std::vector<InstanceBatch> m_batches;

    // Instance buffer per batch (reused across frames)
    std::vector<std::unique_ptr<GLInstanceBuffer>> m_instanceBuffers;

    // Temporary storage for collecting matrices during batch building
    std::vector<glm::mat4> m_matrixScratch;
};

} // namespace Engine
