#include "frame/gl_draw_list.h"

#include "frame/gl_stream_upload.h"

namespace Vkm::Engine {

GLDrawList::GLDrawList()  = default;
GLDrawList::~GLDrawList() = default;

void GLDrawList::clear() {
    m_instances.clear();
    m_commands.clear();
}

void GLDrawList::upload() {
    growAndUpload(
        m_instanceBuffer,
        m_instanceCapacity,
        m_instances.data(),
        static_cast<uint32_t>(m_instances.size() * sizeof(uint32_t))
    );
    growAndUpload(
        m_commandBuffer,
        m_commandCapacity,
        m_commands.data(),
        static_cast<uint32_t>(m_commands.size() * sizeof(DrawCommand))
    );
}

void GLDrawList::draw(const GLMesh& mesh, uint32_t first, uint32_t count) const {
    if (!m_instanceBuffer || !m_commandBuffer) return;
    mesh.pool().drawIndirect(mesh.layout(), *m_instanceBuffer, *m_commandBuffer, first, count);
}

} // namespace Vkm::Engine
