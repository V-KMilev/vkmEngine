#include "gl_mesh.h"

#include "logger.h"

#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_index_buffer.h"
#include "gl_vertex_buffer_layout.h"
#include "gl_error_handle.h"

#include "resource.h"

namespace Engine {

GLMesh::GLMesh(const MeshAsset& mesh) {
    update(mesh);
}

GLMesh::~GLMesh() {
    LOG_TRACE("Destroying GLMesh");

    m_vao.reset();
    m_vbo.reset();
    m_ibo.reset();
}

void GLMesh::update(const MeshAsset& mesh) {
    m_vertexCount = mesh.vertices.size();
    m_indexCount  = mesh.indices.size();

    // Create buffers
    m_vao = std::make_unique<Core::VertexArray>();
    m_vbo = std::make_unique<Core::VertexBuffer>(
        reinterpret_cast<const void*>(mesh.vertices.data()),
        static_cast<uint32_t>(m_vertexCount * sizeof(Vertex))
    );
    m_ibo = std::make_unique<Core::IndexBuffer>(
        reinterpret_cast<const void*>(mesh.indices.data()),
        static_cast<uint32_t>(m_indexCount * sizeof(uint32_t))
    );

    Core::VertexBufferLayout layout;
    layout.push<float>(3);    // position
    layout.push<float>(3);    // normal
    layout.push<float>(2);    // uv
    layout.push<float>(4);    // tangent

    m_vao->addBuffer(*m_vbo, layout);
}

void GLMesh::bind() const {
    m_vao->bind();
    m_ibo->bind();
}

// TODO: Think of how to make it correct
void GLMesh::draw() const {
    bind();

    constexpr const uint32_t drawType = GL_TRIANGLES;
    constexpr const uint32_t indicesOffset = 0;

    VKM_GL_CHECK(glDrawElements(
        drawType,
        static_cast<GLsizei>(m_ibo->getCount()),
        m_ibo->getType(),
        reinterpret_cast<const void*>(static_cast<uintptr_t>(indicesOffset))
    ));
}

} // namespace Engine
