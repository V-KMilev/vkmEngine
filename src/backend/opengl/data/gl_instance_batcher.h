#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gl_instance_buffer.h"

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"

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

        /**
         * @brief The runs from the most recent build*(), for a consumer that did
         *        not build them itself.
         *
         * Lets a batch built once per frame be drawn by more than one pass (the
         * opaque bucket, shared by the depth prepass and the forward pass).
         * Invalidated by the next build*() on this batcher.
         *
         * @return The current run list.
         */
        const std::vector<InstanceRun>& runs() const { return m_runs; }

        void drawRun(const InstanceRun& run);

    private:
        /**
         * @brief Push one drawable's matrices into the flattened arrays.
         *
         * @param d Drawable whose model + normal matrices are appended in run order.
         */
        void append(const DrawableData& d);

        /**
         * @brief Upload the flattened model + normal arrays to the GPU instance buffers.
         */
        void upload();

        std::vector<InstanceRun> m_runs;
        std::vector<uint32_t>    m_order;    ///< sort indices into the input list
        std::vector<glm::mat4>   m_models;   ///< flattened model matrices, run order
        std::vector<glm::mat4>   m_normals;  ///< flattened normal matrices (mat3 -> mat4)

        Core::InstanceBuffer m_modelBuffer;
        Core::InstanceBuffer m_normalBuffer;
};

/**
 * @brief Draw-only access to a batch built by someone else.
 *
 * A batch built once per frame and drawn by several passes (the opaque bucket,
 * shared by the depth prepass and the forward pass) must not be rebuilt by any
 * of them: build*() overwrites the runs and the instance buffers the other pass
 * is about to draw from. Handing out the batcher itself leaves that as a rule in
 * a comment; handing out this view makes it a rule the compiler keeps, because
 * there is no build*() to call.
 *
 * Non-owning and cheap to copy. Valid only while the underlying batcher lives
 * and has not been rebuilt - which, for a frame product, is the frame.
 */
class GLInstanceBatchView {
    public:
        explicit GLInstanceBatchView(GLInstanceBatcher& batcher) : m_batcher(&batcher) {}

        /** @brief The runs to draw, in batch order. */
        const std::vector<InstanceRun>& runs() const { return m_batcher->runs(); }

        /** @brief Draw one run: binds its instance buffers and issues the call. */
        void draw(const InstanceRun& run) const { m_batcher->drawRun(run); }

    private:
        GLInstanceBatcher* m_batcher;
};

} // namespace Engine
