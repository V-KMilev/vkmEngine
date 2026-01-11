#include "gl_backend.h"

#include "gl_context.h"
#include "gl_shader.h"

#include "render_view.h"

namespace Engine {

// TODO: Give access to the context to the user
GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL), m_context() {
    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    m_context.setDefaultState();
    m_context.setFaceCulling(false);
}

void GLBackend::resize(uint32_t width, uint32_t height) {
    m_context.setViewport(0, 0, width, height);
}

} // namespace Engine
