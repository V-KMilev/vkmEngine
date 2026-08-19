#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Vkm::GL {
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;
    class InstanceBuffer;
}

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief GPU copy of a mesh: interleaved VBO + index buffer + the VAO binding them.
 *
 * The vertex layout is fixed (position / normal / uv / tangent at locations
 * 0-3). update() rebuilds the buffers wholesale - simple and fine for the
 * current asset sizes.
 *
 * A skinned mesh carries a second, parallel buffer at locations 8/9 holding its
 * rig binding. It is a separate buffer rather than four more fields on Vertex so
 * that a rock pays nothing for it - the shadow pass reads only aPos and replays
 * every caster per cascade tile and per cube face.
 */
class GLMesh {
    public:
        explicit GLMesh(const MeshAsset& mesh);
        ~GLMesh();

        GLMesh(const GLMesh& other) = delete;
        GLMesh& operator=(const GLMesh& other) = delete;

        GLMesh(GLMesh && other) = delete;
        GLMesh& operator=(GLMesh && other) = delete;

    public:
        void update(const MeshAsset& mesh);
        void draw() const;

        /**
         * @brief Install @p buffer's per-instance matrices as vertex attributes.
         *
         * For a pass whose vertex stage is the cost. The shadow pass redraws
         * the same casters once per light and per cube face with almost no
         * shading, so a hardware attribute fetch is likely to beat the two
         * dependent storage loads attachInstanceIndex costs per vertex - and
         * the CPU work the indirect form would save does not help a frame that
         * is GPU-bound. The forward pass, being fragment-bound, goes the other
         * way; it uses the index path.
         *
         * @param buffer     Per-instance matrices, in draw order.
         * @param startIndex First of the four attribute slots the mat4 spans.
         */
        void attachInstances(Vkm::GL::InstanceBuffer& buffer, uint32_t startIndex) const;

        /**
         * @brief Point the VAO's instance-index attribute at @p buffer.
         *
         * One uint naming which instance this is, and on this path the only
         * per-instance attribute: everything else about an instance is read from
         * storage buffers, so the GPU cull settles one by writing 4 bytes instead
         * of 128, and a run draws its survivors in place. The alternative is
         * attachInstances above, which the shadow pass takes.
         *
         * @param buffer Instance-index buffer; one uint per drawn instance.
         */
        void attachInstanceIndex(const Vkm::GL::VertexBuffer& buffer) const;

        /**
         * @brief Draw @p count instances, reading per-instance attributes from
         * @p baseInstance onward in the attached instance buffer(s).
         */
        void drawInstanced(uint32_t count, uint32_t baseInstance) const;

        /**
         * @brief Draw from a command the GPU wrote, at @p commandOffset bytes
         *        into the currently bound GL_DRAW_INDIRECT_BUFFER.
         *
         * The instance count lives in that command rather than in this call, so
         * a cull running on the GPU can decide it without the CPU learning what
         * it decided.
         */
        void drawIndirect(uint32_t commandOffset) const;

        /// Indices in this mesh, for filling an indirect draw command.
        uint32_t indexCount() const { return static_cast<uint32_t>(m_indexCount); }

        /**
         * @brief Whether this mesh's VAO carries a rig binding at locations 8/9.
         *
         * A property of the GPU layout, which is why the batcher takes the
         * choice of program from here: a run whose vertices have no skin stream
         * can never be drawn by a program that reads one.
         */
        bool isSkinned() const { return m_skinVbo != nullptr; }

    private:
        std::unique_ptr<Vkm::GL::VertexArray>  m_vao;
        std::unique_ptr<Vkm::GL::VertexBuffer> m_vbo;
        std::unique_ptr<Vkm::GL::VertexBuffer> m_skinVbo;  ///< Per-vertex rig binding; null when unskinned.
        std::unique_ptr<Vkm::GL::IndexBuffer>  m_ibo;
        size_t m_indexCount = 0;
};

} // namespace Vkm::Engine
