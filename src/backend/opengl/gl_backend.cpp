#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <string>

#include <GL/glew.h>

#include "logger.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_composite_pass.h"
#include "system/render/render_view.h"

namespace Engine {

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL) {}

GLBackend::~GLBackend() = default;

bool GLBackend::init(WindowManager& window) {
    (void)window;  // GLEW + the GL context are created during Window creation;
                   // we draw into the already-current context. Presentation
                   // (buffer swap) stays in the engine loop, so we never swap.

    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    m_context.setDefaultState();
    m_context.setDepthTest(true);
    m_context.setFaceCulling(false);

    // Build the pass list. The forward pass compiles its shader, so this must
    // run after the context exists.
    m_passes.push_back(std::make_unique<GLForwardPass>());
    m_passes.push_back(std::make_unique<GLCompositePass>());

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* device  = glGetString(GL_RENDERER);
    m_info.api    = version ? "OpenGL " + std::string(reinterpret_cast<const char*>(version)) : "OpenGL";
    m_info.device = device  ? std::string(reinterpret_cast<const char*>(device)) : "";
    LOG_INFO("%s on %s", m_info.api.c_str(), m_info.device.c_str());

    return true;
}

void GLBackend::resize(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    m_context.setViewport(
        static_cast<int32_t>(x),
        static_cast<int32_t>(y),
        static_cast<int32_t>(width),
        static_cast<int32_t>(height)
    );
}

void GLBackend::render(const RenderView& view, const ResourceManager& resources) {
    m_view.sync(view, resources);
    m_sceneHDR.resize(view.viewportWidth, view.viewportHeight);

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    m_camera.update(view.camera);
    m_lights.update(view.lights);

    // Each pass binds and clears its own target: the forward pass renders the
    // scene into m_sceneHDR; the composite pass tonemaps it to the backbuffer.
    GLFrameContext ctx{view, m_view, m_context, m_sceneHDR};
    for (const auto& pass : m_passes) {
        pass->execute(ctx);
    }
}

} // namespace Engine
