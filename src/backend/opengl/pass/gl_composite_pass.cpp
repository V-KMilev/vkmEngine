#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_composite_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "system/render/render_view.h"

namespace Engine {

GLCompositePass::GLCompositePass()
    : m_shader(std::make_unique<Core::Shader>("shaders/composite")) {}

GLCompositePass::~GLCompositePass() = default;

void GLCompositePass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view = ctx.view;

    // Back to the backbuffer, into the window's viewport rect. The default
    // framebuffer is not a GL object, so this one bind stays raw.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ctx.gl.setViewport(
        static_cast<int32_t>(view.viewportX),
        static_cast<int32_t>(view.viewportY),
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight)
    );
    ctx.gl.setDepthTest(false);

    m_shader->bind();
    ctx.sceneHDR.bindColor(0);
    m_triangle.draw();
}

} // namespace Engine
