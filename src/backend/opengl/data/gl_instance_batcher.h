#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <memory>

#include "gl_instance_buffer.h"
#include "gl_shader_storage_buffer.h"

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
 * @brief Growable buffer of per-instance indices.
 *
 * Vertex storage, because the index arrives as an attribute so that GL's
 * baseInstance offsets it per run; also bound as a storage buffer, because the
 * cull writes into it and the vertex stage reads through it.
 */
class InstanceIndexBuffer {
    public:
        InstanceIndexBuffer() = default;
        ~InstanceIndexBuffer() = default;

        InstanceIndexBuffer(const InstanceIndexBuffer& other) = delete;
        InstanceIndexBuffer& operator=(const InstanceIndexBuffer& other) = delete;

        InstanceIndexBuffer(InstanceIndexBuffer && other) = delete;
        InstanceIndexBuffer& operator=(InstanceIndexBuffer && other) = delete;

    public:
        /// Upload @p bytes of indices, growing the storage when it must.
        void update(const void* data, uint32_t bytes);

        const Core::VertexBuffer& buffer() const { return *m_buffer; }
        bool     valid() const { return m_buffer != nullptr; }
        uint32_t id()    const;

    private:
        std::unique_ptr<Core::VertexBuffer> m_buffer;
        uint32_t m_capacity = 0;
};

/**
 * @brief One indirect draw, laid out exactly as GL reads it.
 *
 * The GPU cull writes instanceCount and nothing else; the CPU fills the rest
 * when it builds the runs. Keeping the count in the buffer GL already reads is
 * what lets the cull decide it without the CPU ever learning the answer.
 */
struct DrawCommand {
    uint32_t count         = 0;   ///< Indices per instance.
    uint32_t instanceCount = 0;   ///< Written by the cull, read by the draw.
    uint32_t firstIndex    = 0;
    uint32_t baseVertex    = 0;
    uint32_t baseInstance  = 0;   ///< Start of this run's slice of the instance buffers.
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

        /**
         * @brief Draw one run.
         *
         * Indirect when a cull has run over this batch this frame, so the count
         * comes from the command the GPU wrote; a plain instanced draw
         * otherwise. Which it is belongs here rather than at the call sites:
         * the same drawing loop serves the culled opaque batch and the private
         * alpha-mask and transparent ones, and a caller cannot tell them apart.
         *
         * @param run      The run to draw.
         * @param runIndex Its index in runs(), which selects its command.
         */
        void drawRun(const InstanceRun& run, uint32_t runIndex);

        /**
         * @brief Bind the cull's inputs and outputs to their SSBO points.
         *
         * Called by the cull pass before dispatching; the batcher owns the
         * buffers because it owns the instance data they mirror.
         *
         * @return False when there is nothing to cull.
         */
        bool bindCullBuffers();

        /**
         * @brief Bind this batch's transforms and index list for drawing.
         *
         * Once per batch rather than per run: every run in a batch reads the
         * same three buffers, and only the index attribute's baseInstance moves
         * between them.
         */
        void bindInstanceData() const;

        /// Instances uploaded this frame - the cull's dispatch size.
        uint32_t instanceCount() const { return static_cast<uint32_t>(m_models.size()); }

        /**
         * @brief Reset every run's instance count to zero and upload the commands.
         *
         * The cull accumulates into these counts, so they have to start empty;
         * doing it here rather than in a clearing dispatch keeps the upload the
         * CPU already does as the only write.
         */
        void resetCommands();

    private:
        /**
         * @brief Push one drawable's matrices into the flattened arrays.
         *
         * @param d Drawable whose model + normal matrices are appended in run order.
         * @param runIndex The run being filled, recorded per instance so the
         *                 cull knows which command to count into.
         */
        void append(const DrawableData& d, uint32_t runIndex);

        /**
         * @brief Upload the flattened model + normal arrays to the GPU instance buffers.
         */
        void upload();

        /**
         * @brief Grow-or-update an SSBO to hold @p bytes of @p data.
         *
         * SSBOs size to the frame's instance count, which moves; reallocating
         * only when it grows keeps a steady scene at one allocation.
         */
        static void uploadStorage(std::unique_ptr<Core::ShaderStorageBuffer>& buffer,
                                  uint32_t& capacity, const void* data, uint32_t bytes);

    private:
        std::vector<InstanceRun> m_runs;
        std::vector<uint32_t>    m_order;    ///< sort indices into the input list
        std::vector<glm::mat4>   m_models;   ///< flattened model matrices, run order
        std::vector<glm::mat4>   m_normals;  ///< flattened normal matrices (mat3 -> mat4)

        std::vector<glm::vec4> m_bounds;    ///< World AABB per instance, min then max.
        std::vector<uint32_t>  m_visible;   ///< Identity, until a cull replaces it with the survivors.
        std::vector<uint32_t>  m_runOf;     ///< Which run each instance belongs to.
        std::vector<DrawCommand> m_commands;///< One per run, in runs() order.

        Core::InstanceBuffer m_modelBuffer;   ///< Every instance's model matrix, batch order.
        Core::InstanceBuffer m_normalBuffer;  ///< Every instance's normal matrix, batch order.
        InstanceIndexBuffer  m_visibleBuffer; ///< Which of them each drawn instance is.

        std::unique_ptr<Core::ShaderStorageBuffer> m_boundsBuffer;
        std::unique_ptr<Core::ShaderStorageBuffer> m_runOfBuffer;
        std::unique_ptr<Core::ShaderStorageBuffer> m_commandBuffer;
        bool     m_culled          = false;  ///< A cull filled the commands this frame, so draws go indirect.
        uint32_t m_boundsCapacity  = 0;
        uint32_t m_runOfCapacity   = 0;
        uint32_t m_commandCapacity = 0;
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

        /**
         * @brief The runs to draw, in batch order.
         */
        const std::vector<InstanceRun>& runs() const { return m_batcher->runs(); }

        /**
         * @brief The batcher itself, for the cull pass.
         *
         * The cull writes into the batch - it compacts the instance buffers and
         * fills the draw commands - so a draw-only view cannot express it. What
         * the view protects against is a *rebuild* invalidating the runs another
         * pass is about to draw, and the cull does not rebuild.
         */
        GLInstanceBatcher& batcher() const { return *m_batcher; }

        /**
         * @brief Draw one run: binds its instance buffers and issues the call.
         * @param runIndex Its index in runs(); selects the run's indirect command.
         */
        void draw(const InstanceRun& run, uint32_t runIndex) const { m_batcher->drawRun(run, runIndex); }

        /// Bind the batch's transforms and index list; call once before the run loop.
        void bindInstanceData() const { m_batcher->bindInstanceData(); }

    private:
        GLInstanceBatcher* m_batcher;
};

} // namespace Engine
