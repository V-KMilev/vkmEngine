#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "resource/resource_handle.h"
#include "resource/mesh_asset.h"
#include "resource/material_asset.h"

#include "gl_instance_buffer.h"  // Core::InstanceBuffer (vkmGL)

namespace Core { class VertexArray; }

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
    MaterialType materialType = MaterialType::Opaque;
    uint32_t instanceCount        = 0;
    uint32_t shadowInstanceCount  = 0;  ///< Leading prefix of instances that cast shadows.
    uint32_t firstInstance        = 0;  ///< Offset into the shared instance buffer.
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

        GLInstanceBatcher(const GLInstanceBatcher& other) = delete;
        GLInstanceBatcher& operator=(const GLInstanceBatcher& other) = delete;
        GLInstanceBatcher(GLInstanceBatcher && other) = delete;
        GLInstanceBatcher& operator=(GLInstanceBatcher && other) = delete;

    public:
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
        * @brief Bind this batcher's instance matrices onto @p vao.
        *
        * Owns the cross-batcher VAO arbitration policy: meshes share one VAO,
        * but the camera batcher and the shadow batcher are distinct buffers,
        * so the VAO's instanced attributes must be re-pointed whenever the
        * other batcher used that VAO since. Tracked globally; a no-op while
        * this batcher still owns the VAO. (The Core::InstanceBuffer itself is
        * deliberately policy-free.)
        */
        void attachToVAO(Core::VertexArray& vao, uint32_t startIndex = 4);

        /**
        * @brief Clears all batches (called at frame start).
        */
        void clear();

    private:
        std::vector<InstanceBatch> m_batches;
        std::vector<glm::mat4> m_matrixScratch;
        Core::InstanceBuffer m_buffer;

        /**
         * @brief Cross-batcher VAO arbitration. Meshes share one VAO across the
         *
         * camera and shadow batchers, but the VAO's instanced attributes can
         * only point at one buffer at a time; this map records the most
         * recent owner so attachToVAO is a no-op while we still own it and a
         * real rebind when another batcher used the VAO since.
         *
         * Lives on the class (rather than as a file-local global) so the
         * arbitration state is reachable for inspection / tests if needed.
         */
        static std::unordered_map<uint32_t, const GLInstanceBatcher*> s_vaoOwner;
};

} // namespace Engine
