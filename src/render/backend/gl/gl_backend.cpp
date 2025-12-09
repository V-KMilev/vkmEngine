#include "gl_backend.h"

#include "gl_context.h"
#include "gl_shader.h"

#include "render_view.h"
#include "resource_manager.h"

namespace Engine {

GLBackend::GLBackend(
    Core::Context& context
) : RenderBackend(RenderBackendType::OpenGL),
    m_context(context) {}

void GLBackend::resize(uint32_t width, uint32_t height) {
    m_context.setViewport(0, 0, width, height);
}

} // namespace Engine