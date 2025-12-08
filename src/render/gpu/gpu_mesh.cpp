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
) : m_source(&cpuMesh) {
    upload();
}

void GPUMesh::setSource(const CPUMesh& cpuMesh) {
    m_source = &cpuMesh;
}

void GPUMesh::upload() {
    if (!m_source) {
        return;
    }

    m_vertexCount = m_source->getVertices().size();
    m_indexCount = m_source->getIndices().size();

    // Create buffers
    m_vao = std::make_unique<Core::VertexArray>();
    m_vbo = std::make_unique<Core::VertexBuffer>(
        reinterpret_cast<const void*>(m_source->getVertices().data()),
        static_cast<uint32_t>(m_vertexCount * sizeof(Vertex))
    );
    m_ibo = std::make_unique<Core::IndexBuffer>(
        reinterpret_cast<const void*>(m_source->getIndices().data()),
        static_cast<uint32_t>(m_indexCount * sizeof(uint32_t))
    );

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
        renderer.draw(*m_vao, shader, GL_TRIANGLES, 0, static_cast<uint32_t>(m_vertexCount));
    }
}

} // namespace Engine
