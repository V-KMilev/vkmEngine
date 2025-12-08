#pragma once

#include <memory>
#include <cstdint>

namespace Core {
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;

    class Renderer;
    class Shader;
}

namespace Engine {
    class CPUMesh;
}

namespace Engine {

/**
 * @class GPUMesh
 * @brief Manages a mesh's vertex/index data on the GPU and supports drawing.
 *
 * The GPUMesh class holds GPU resources corresponding to a mesh (VAO, VBO, IBO)
 * and provides methods for uploading data from a CPUMesh and issuing draw calls
 * using a given renderer and shader.
 *
 * Copy and move operations are disabled to ensure GPU resource stability.
 */
class GPUMesh {
    public:
        GPUMesh() = delete;
        ~GPUMesh() = default;

        GPUMesh(const GPUMesh& other) = delete;
        GPUMesh& operator=(const GPUMesh& other) = delete;

        GPUMesh(GPUMesh && other) = delete;
        GPUMesh& operator=(GPUMesh && other) = delete;

        explicit GPUMesh(const CPUMesh& cpuMesh);

    public:
        /**
         * @brief Sets a new source CPUMesh for this GPUMesh.
         * This does NOT upload the data to the GPU automatically.
         * @param cpuMesh The new CPU mesh to use as data source.
         */
        void setSource(const CPUMesh& cpuMesh);

        /**
         * @brief Uploads mesh data from the referenced CPUMesh to the GPU.
         * Allocates and fills GPU buffers as needed.
         */
        void upload();

        /**
         * @brief Draws the mesh using the given renderer and shader.
         * Issues the appropriate draw call using the uploaded GPU data.
         * @param renderer Reference to the renderer used to submit commands.
         * @param shader Reference to the shader program to use.
         */
        void draw(const Core::Renderer& renderer, const Core::Shader& shader) const;

    private:
        const CPUMesh* m_source;
        size_t m_indexCount;
        size_t m_vertexCount;

        std::unique_ptr<Core::VertexArray> m_vao;
        std::unique_ptr<Core::VertexBuffer> m_vbo;
        std::unique_ptr<Core::IndexBuffer> m_ibo;
};

} // namespace Engine
