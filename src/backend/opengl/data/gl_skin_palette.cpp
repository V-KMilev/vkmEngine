#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_skin_palette.h"

#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"
#include "data/gl_stream_upload.h"

namespace Vkm::Engine {

GLSkinPalette::~GLSkinPalette() = default;

void GLSkinPalette::update(const std::vector<glm::mat4>& matrices) {
    m_count = static_cast<uint32_t>(matrices.size());
    growAndUpload(m_buffer, m_capacity, matrices.data(),
                  static_cast<uint32_t>(matrices.size() * sizeof(glm::mat4)));
}

void GLSkinPalette::bind() const {
    if (!m_buffer) return;
    m_buffer->bindBase(GLBindings::SSBOBindingPoints::SkinPalette);
}

} // namespace Vkm::Engine
