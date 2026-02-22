#include "gl_backend.h"

#include "logger.h"

#include "gl_context.h"
#include "gl_shader.h"

#include "render/render_view.h"

namespace Engine {

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL), m_context() {
    // GLEW is initialized during Window creation (before any GL calls)

    // Set default clear color (dark gray)
    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    
    // Initialize default OpenGL state
    m_context.setDefaultState();
    
    // Disable face culling by default (can be overridden by render passes)
    m_context.setFaceCulling(false);
}

void GLBackend::resize(uint32_t width, uint32_t height) {
    m_context.setViewport(0, 0, width, height);
    m_defaultTarget.resize(width, height);
}

} // namespace Engine
