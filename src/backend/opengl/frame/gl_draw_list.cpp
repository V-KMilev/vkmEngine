#include "frame/gl_draw_list.h"

namespace Vkm::Engine {

GLDrawList::GLDrawList()  = default;
GLDrawList::~GLDrawList() = default;

void GLDrawList::clear() {
    m_instances.clear();
    m_commands.clear();
}

void GLDrawList::upload(Vkm::GL::StreamRing& stream) {
    const auto bytes = [](const auto& list) { return static_cast<uint32_t>(list.size() * sizeof(list[0])); };
    m_instanceSpan = stream.write(m_instances.data(), bytes(m_instances));
    m_commandSpan  = stream.write(m_commands.data(), bytes(m_commands));
}

void GLDrawList::draw(const GLMesh& mesh, uint32_t first, uint32_t count) const {
    if (!m_instanceSpan || !m_commandSpan) return;
    mesh.pool().drawIndirect(mesh.layout(), m_instanceSpan, m_commandSpan, first, count);
}

} // namespace Vkm::Engine
