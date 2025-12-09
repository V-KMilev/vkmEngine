#pragma once

#include <memory>
#include <cstdint>

namespace Core {
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;
}

namespace Engine {
    struct MeshAsset;
}

namespace Engine {

/**
 * @brief Encapsulates an OpenGL mesh, managing VAO, VBO, and IBO.
 *
 * GLMesh maintains the GPU-side resources for rendering a mesh.
 * It prohibits copy/move semantics to ensure unique OpenGL state ownership.
 */
class GLMesh {
    public:
        GLMesh() = delete;
        ~GLMesh();

        GLMesh(const GLMesh& other) = delete;
        GLMesh& operator=(const GLMesh& other) = delete;

        GLMesh(GLMesh && other) = delete;
        GLMesh& operator=(GLMesh && other) = delete;

        /**
         * @brief Constructs a mesh from the provided asset and uploads data to GPU.
         * @param mesh Reference to the mesh asset to be uploaded.
         */
        GLMesh(const MeshAsset& mesh);

    public:
        /**
         * @brief Returns the number of vertices in the mesh.
         * @return Vertex count.
         */
         size_t getVertexCount() const { return m_vertexCount; }

         /**
          * @brief Returns the number of indices in the mesh.
          * @return Index count.
          */
         size_t getIndexCount() const { return m_indexCount; }

        /**
         * @brief Updates the underlying mesh data on the GPU using a new asset.
         * @param mesh Reference to the new mesh asset.
         */
        void update(const MeshAsset& mesh);

        /**
         * @brief Binds the VAO and IBO in preparation for rendering.
         */
        void bind() const;

        /**
         * @brief Issues an OpenGL draw call using current mesh data.
         */
        void draw() const;

    private:
        size_t m_indexCount;
        size_t m_vertexCount;

        std::unique_ptr<Core::VertexArray> m_vao;
        std::unique_ptr<Core::VertexBuffer> m_vbo;
        std::unique_ptr<Core::IndexBuffer> m_ibo;
};

} // namespace Engine
