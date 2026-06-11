#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gl_instance_buffer.h"  // Core::InstanceBuffer (header-only)

#include "resource/asset/mesh_asset.h"      // MeshHandle
#include "resource/asset/material_asset.h"  // MaterialHandle

namespace Engine {

class GLMesh;
class GLView;
struct DrawableData;

/**
 * @brief One instanced draw: a (material, mesh) run of consecutive instances.
 *
 * `first` is the baseInstance into the batcher's uploaded model/normal buffers;
 * `count` instances are drawn from there.
 */
struct InstanceRun {
    const GLMesh*  mesh     = nullptr;
    MaterialHandle material;
    uint32_t       first    = 0;
    uint32_t       count    = 0;
};

/**
 * @brief Groups sorted drawables into instanced draws.
 *
 * build*() flattens each drawable's model + normal matrix into two GPU instance
 * buffers (one upload per build) and returns the runs to draw. drawRun() binds
 * those buffers onto the run's mesh VAO and issues one instanced draw; the caller
 * binds material/shader state per run first.
 *
 * Two grouping modes:
 *  - buildGrouped: sort by (material, mesh) and merge identical draws into one
 *    instanced call each. For order-independent buckets (opaque).
 *  - buildSequential: one instance per run, input order preserved. For
 *    order-dependent buckets (back-to-front transparents).
 *
 * The returned run list is valid only until the next build*() on this batcher.
 */
class GLInstanceBatcher {
    public:
        GLInstanceBatcher()  = default;
        ~GLInstanceBatcher() = default;

        GLInstanceBatcher(const GLInstanceBatcher& other) = delete;
        GLInstanceBatcher& operator=(const GLInstanceBatcher& other) = delete;

        GLInstanceBatcher(GLInstanceBatcher && other) = delete;
        GLInstanceBatcher& operator=(GLInstanceBatcher && other) = delete;

    public:
        const std::vector<InstanceRun>& buildGrouped(
            const std::vector<const DrawableData*>& list, const GLView& view);

        const std::vector<InstanceRun>& buildSequential(
            const std::vector<const DrawableData*>& list, const GLView& view);

        void drawRun(const InstanceRun& run);

    private:
        /// Push one drawable's matrices into the flattened arrays.
        void append(const DrawableData& d);
        /// Upload the flattened model + normal arrays to the GPU instance buffers.
        void upload();

        std::vector<InstanceRun> m_runs;
        std::vector<uint32_t>    m_order;    ///< sort indices into the input list
        std::vector<glm::mat4>   m_models;   ///< flattened model matrices, run order
        std::vector<glm::mat4>   m_normals;  ///< flattened normal matrices (mat3 -> mat4)

        Core::InstanceBuffer m_modelBuffer;
        Core::InstanceBuffer m_normalBuffer;
};

} // namespace Engine
