#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Core {
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;
    class InstanceBuffer;
}

namespace Engine {

struct MeshAsset;

/**
 * @brief GPU copy of a mesh: interleaved VBO + index buffer + the VAO binding them.
 *
 * The vertex layout is fixed (position / normal / uv / tangent at locations
 * 0-3). update() rebuilds the buffers wholesale - simple and fine for the
 * current asset sizes.
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

        /// Install @p buffer's per-instance attributes onto this mesh's VAO,
        /// starting at attribute @p startIndex (a mat4 spans startIndex..+3,
        /// divisor 1). Re-points each call; the caller decides when to re-attach.
        void attachInstances(Core::InstanceBuffer& buffer, uint32_t startIndex) const;

        /// Draw @p count instances, reading per-instance attributes from
        /// @p baseInstance onward in the attached instance buffer(s).
        void drawInstanced(uint32_t count, uint32_t baseInstance) const;

    private:
        std::unique_ptr<Core::VertexArray>  m_vao;
        std::unique_ptr<Core::VertexBuffer> m_vbo;
        std::unique_ptr<Core::IndexBuffer>  m_ibo;
        size_t m_indexCount = 0;
};

} // namespace Engine
