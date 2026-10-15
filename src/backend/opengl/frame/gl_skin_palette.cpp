#include "frame/gl_skin_palette.h"

#include "gl_stream_ring.h"

#include "convention/gl_bindings.h"

namespace Vkm::Engine {

GLSkinPalette::GLSkinPalette()  = default;
GLSkinPalette::~GLSkinPalette() = default;

void GLSkinPalette::update(const std::vector<glm::mat4>* matrices, Vkm::GL::StreamRing& stream) {
    m_count = matrices ? static_cast<uint32_t>(matrices->size()) : 0u;
    if (m_count == 0) return;
    m_device.upload(stream, matrices->data(), m_count * static_cast<uint32_t>(sizeof(glm::mat4)));
}

void GLSkinPalette::bind() const {
    const Vkm::GL::BufferSpan span = m_device.span();
    if (!span) return;
    Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, GLBindings::SSBOBindingPoints::SKIN_PALETTE, span);
}

} // namespace Vkm::Engine
