#include "frame/gl_skin_palette.h"

#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"
#include "frame/gl_stream_upload.h"

namespace Vkm::Engine {

GLSkinPalette::GLSkinPalette()  = default;
GLSkinPalette::~GLSkinPalette() = default;

void GLSkinPalette::update(const std::vector<glm::mat4>* matrices) {
    m_count = matrices ? static_cast<uint32_t>(matrices->size()) : 0u;
    if (m_count == 0) return;
    growAndUpload(m_buffer, m_capacity, matrices->data(), m_count * static_cast<uint32_t>(sizeof(glm::mat4)));
}

void GLSkinPalette::bind() const {
    if (!m_buffer) return;
    m_buffer->bindBase(GLBindings::SSBOBindingPoints::SKIN_PALETTE);
}

} // namespace Vkm::Engine
