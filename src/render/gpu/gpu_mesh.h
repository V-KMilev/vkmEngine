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
        void draw(const Core::Renderer& renderer, const Core::Shader& shader) const;

    private:
        std::unique_ptr<Core::VertexArray> m_vao;
        std::unique_ptr<Core::VertexBuffer> m_vbo;
        std::unique_ptr<Core::IndexBuffer> m_ibo;

        size_t m_indexCount;
        size_t m_vertexCount;
    };
} // namespace Engine
