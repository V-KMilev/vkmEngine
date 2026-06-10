#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_mesh.h"

#include <cstdint>

#include <GL/glew.h>

#include "gl_error_handle.h"
#include "gl_vertex_array.h"
#include "gl_vertex_buffer.h"
#include "gl_vertex_buffer_layout.h"
#include "gl_index_buffer.h"
#include "gl_instance_buffer.h"

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

void GLMesh::attachInstances(Core::InstanceBuffer& buffer, uint32_t startIndex) const {
    if (m_vao) buffer.attachToVAO(*m_vao, startIndex);
}

void GLMesh::drawInstanced(uint32_t count, uint32_t baseInstance) const {
    if (!m_vao || m_indexCount == 0 || count == 0) return;

    m_vao->bind();
    m_ibo->bind();
    // baseInstance offsets which per-instance attribute element each instance
    // reads, so all runs can share one uploaded buffer (GL 4.2+ / ARB_base_instance).
    VKM_GL_CHECK(glDrawElementsInstancedBaseInstance(
        GL_TRIANGLES,
        static_cast<GLsizei>(m_indexCount),
        GL_UNSIGNED_INT,
        nullptr,
        static_cast<GLsizei>(count),
        baseInstance
    ));
}

} // namespace Engine
