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

namespace Vkm::Engine {

namespace {
// Matches layout(location = 4) in shaders/_common/instancing.glsl.
constexpr uint32_t INSTANCE_INDEX_ATTRIB = 4;
// Matches layout(location = 8/9) in shaders/_common/skinning.glsl.
constexpr uint32_t SKIN_ATTRIB = 8;
} // namespace

GLMesh::GLMesh(const MeshAsset& mesh) {
    update(mesh);
}

GLMesh::~GLMesh() = default;

void GLMesh::update(const MeshAsset& mesh) {
    m_indexCount = static_cast<uint32_t>(mesh.indices.size());

    const uint32_t vertexBytes = static_cast<uint32_t>(mesh.vertices.size() * sizeof(Vertex));
    m_vbo = std::make_unique<Vkm::GL::VertexBuffer>(mesh.vertices.data(), vertexBytes);
    m_ibo = std::make_unique<Vkm::GL::IndexBuffer>(mesh.indices.data(), m_indexCount);

    // Interleaved vertex layout - must match the `in` attributes in the shader.
    Vkm::GL::VertexBufferLayout layout;
    layout.push<float>(3);  // position (location 0)
    layout.push<float>(3);  // normal   (location 1)
    layout.push<float>(2);  // uv       (location 2)
    layout.push<float>(4);  // tangent  (location 3)

    m_vao = std::make_unique<Vkm::GL::VertexArray>();
    m_vao->addBuffer(*m_vbo, layout);

    // A mesh is skinned exactly when the asset carries a skin stream - no
    // component says so. The stream is parallel to the vertices and sits at
    // divisor 0, which is also why leaving 8/9 enabled costs nothing when the
    // static program draws this VAO: every fetch is indexed by a vertex index
    // this buffer has an entry for. GLSceneCapture and GLPreview rely on that -
    // they draw shaders/forward/pbr, so a character bakes and thumbnails in bind
    // pose, which is the right answer for both.
    if (mesh.skin.empty()) {
        m_skinVbo.reset();
        return;
    }

    const uint32_t skinBytes = static_cast<uint32_t>(mesh.skin.size() * sizeof(SkinVertex));
    m_skinVbo = std::make_unique<Vkm::GL::VertexBuffer>(mesh.skin.data(), skinBytes);

    Vkm::GL::VertexBufferLayout skinLayout;
    skinLayout.push<uint16_t>(4);  // bone indices (location 8), unnormalized: they stay integers
    skinLayout.push<uint8_t>(4);   // weights      (location 9), unorm8: they arrive summing to 1.0
    m_vao->addBuffer(*m_skinVbo, skinLayout, SKIN_ATTRIB);
}

void GLMesh::draw() const {
    if (!m_vao || m_indexCount == 0) return;

    m_vao->bind();
    m_ibo->bind();
    m_ibo->draw();
}

void GLMesh::attachInstances(Vkm::GL::InstanceBuffer& buffer, uint32_t startIndex) const {
    if (!m_vao) return;
    buffer.attachToVAO(*m_vao, startIndex);
}

void GLMesh::attachInstanceIndex(const Vkm::GL::VertexBuffer& buffer) const {
    if (!m_vao) return;

    m_vao->bind();
    buffer.bind();

    // The mat4 path leaves 5..7 enabled on this shared VAO, and an enabled array
    // the shader never declares is still an array GL may fetch - past the end of
    // a buffer sized for a different pass. Turn them off rather than rely on the
    // driver skipping them.
    for (uint32_t slot = INSTANCE_INDEX_ATTRIB + 1; slot <= INSTANCE_INDEX_ATTRIB + 3; ++slot)
        VKM_GL_CHECK(glDisableVertexAttribArray(slot));
    // An integer attribute, so glVertexAttribIPointer: the float entry point
    // would convert the index to a float and silently lose slots past 2^24.
    VKM_GL_CHECK(glEnableVertexAttribArray(INSTANCE_INDEX_ATTRIB));
    VKM_GL_CHECK(glVertexAttribIPointer(INSTANCE_INDEX_ATTRIB, 1, GL_UNSIGNED_INT,
                                        sizeof(uint32_t), nullptr));
    m_vao->setAttributeDivisor(INSTANCE_INDEX_ATTRIB, 1);
}

void GLMesh::drawInstanced(uint32_t count, uint32_t baseInstance) const {
    if (!m_vao || m_indexCount == 0 || count == 0) return;

    m_vao->bind();
    m_ibo->bind();
    // baseInstance offsets which per-instance attribute element each instance
    // reads, so all runs can share one uploaded buffer (GL 4.2+ / ARB_base_instance).
    m_ibo->drawInstanced(count, baseInstance);
}

void GLMesh::drawIndirect(uint32_t commandOffset) const {
    if (!m_vao || m_indexCount == 0) return;

    m_vao->bind();
    m_ibo->bind();
    VKM_GL_CHECK(glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(static_cast<uintptr_t>(commandOffset))));
}

} // namespace Vkm::Engine
