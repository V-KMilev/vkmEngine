#include "gpu_mesh.h"

#include "logger.h"

#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_index_buffer.h"
#include "gl_vertex_buffer_layout.h"
#include "cpu_mesh.h"

#include "gl_render.h"
#include "gl_shader.h"

namespace Engine {

GPUMesh::GPUMesh(
    const CPUMesh& cpuMesh
) : m_vao(std::make_unique<Core::VertexArray>()),
    m_vbo(std::make_unique<Core::VertexBuffer>(
        reinterpret_cast<const void*>(cpuMesh.getVertices().data()),
        static_cast<uint32_t>(cpuMesh.getVertices().size() * sizeof(Vertex))
    )),
    m_ibo(std::make_unique<Core::IndexBuffer>(
        reinterpret_cast<const void*>(cpuMesh.getIndices().data()),
        static_cast<uint32_t>(cpuMesh.getIndices().size() * sizeof(uint32_t))
    )),
    m_indexCount(cpuMesh.getIndices().size()),
    m_vertexCount(cpuMesh.getVertices().size()) {

    Core::VertexBufferLayout layout;
    layout.push<float>(3);    // position
    layout.push<float>(3);    // normal
    layout.push<float>(2);    // uv

    m_vao->addBuffer(*m_vbo, layout);
}

void GPUMesh::draw(const Core::Renderer& renderer, const Core::Shader& shader) const {
    // TODO: Implement this function when we have real render system ready!
    if (!m_vao) {
        LOG_ERROR("Failed to draw mesh: VAO is not set");
        return;
    }

    if (m_ibo) {
        renderer.draw(*m_vao, *m_ibo, shader);
    }
    else {
        // renderer.draw(*m_vao, shader);
    }
}
} // namespace Engine
