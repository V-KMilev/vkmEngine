#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_mesh.h"

#include <cstdint>

#include <GL/glew.h>

#include "gl_error_handle.h"
#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_vertex_buffer_layout.h"
#include "gl_index_buffer.h"

#include "resource/asset/mesh_asset.h"

namespace Engine {

GLMesh::GLMesh(const MeshAsset& mesh) {
    update(mesh);
}

GLMesh::~GLMesh() = default;

void GLMesh::update(const MeshAsset& mesh) {
    m_indexCount = static_cast<uint32_t>(mesh.indices.size());

    const uint32_t vertexBytes = static_cast<uint32_t>(mesh.vertices.size() * sizeof(Vertex));
    m_vbo = std::make_unique<Core::VertexBuffer>(mesh.vertices.data(), vertexBytes);
    m_ibo = std::make_unique<Core::IndexBuffer>(mesh.indices.data(), m_indexCount);

    // Interleaved vertex layout - must match the `in` attributes in the shader.
    Core::VertexBufferLayout layout;
    layout.push<float>(3);  // position (location 0)
    layout.push<float>(3);  // normal   (location 1)
    layout.push<float>(2);  // uv       (location 2)
    layout.push<float>(4);  // tangent  (location 3)

    m_vao = std::make_unique<Core::VertexArray>();
    m_vao->addBuffer(*m_vbo, layout);
}

void GLMesh::draw() const {
    if (!m_vao || m_indexCount == 0) return;

    m_vao->bind();
    m_ibo->bind();
    VKM_GL_CHECK(glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(m_indexCount),
        GL_UNSIGNED_INT,
        nullptr
    ));
}

} // namespace Engine
