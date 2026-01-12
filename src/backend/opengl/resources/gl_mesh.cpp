#include "gl_mesh.h"

#include "logger.h"

#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_index_buffer.h"
#include "gl_vertex_buffer_layout.h"

#include "gl_error_handle.h"

#include "mesh_asset.h"

namespace Engine {

GLMesh::GLMesh(const MeshAsset& mesh) {
    update(mesh);
}

GLMesh::~GLMesh() {
    m_vao.reset();
    m_vbo.reset();
    m_ibo.reset();

    LOG_TRACE("Destroying GLMesh");
}

void GLMesh::update(const MeshAsset& mesh) {
    m_vertexCount = mesh.vertices.size();
    m_indexCount  = mesh.indices.size();

    const uint32_t vertexDataSize = static_cast<uint32_t>(m_vertexCount * sizeof(Vertex));
    const uint32_t indexDataSize  = static_cast<uint32_t>(m_indexCount * sizeof(uint32_t));

    if (m_vbo && m_vbo->getSize() == vertexDataSize) {
        m_vbo->update(mesh.vertices.data(), vertexDataSize);
    } else {
        m_vbo = std::make_unique<Core::VertexBuffer>(reinterpret_cast<const void*>(mesh.vertices.data()), vertexDataSize);

        // Recreate VAO to reflect new VBO
        m_vao.reset();
    }

    if (m_ibo && m_ibo->getSize() == indexDataSize) {
        m_ibo->update(mesh.indices.data(), indexDataSize);
    } else {
        m_ibo = std::make_unique<Core::IndexBuffer>(reinterpret_cast<const void*>(mesh.indices.data()), indexDataSize);
    }

    // Create or recreate VAO if needed (when VBO is created/recreated)
    if (!m_vao) {
        m_vao = std::make_unique<Core::VertexArray>();

        Core::VertexBufferLayout layout;
        layout.push<float>(3);    // position
        layout.push<float>(3);    // normal
        layout.push<float>(2);    // uv
        layout.push<float>(4);    // tangent

        m_vao->addBuffer(*m_vbo, layout);
    }
}

void GLMesh::bind() const {
    m_vao->bind();
    m_ibo->bind();
}

void GLMesh::draw(int drawType) const {
    bind();

    constexpr const uint32_t indicesOffset = 0;

    VKM_GL_CHECK(glDrawElements(
        drawType,
        static_cast<GLsizei>(m_ibo->getCount()),
        m_ibo->getType(),
        reinterpret_cast<const void*>(static_cast<uintptr_t>(indicesOffset))
    ));
}

} // namespace Engine
