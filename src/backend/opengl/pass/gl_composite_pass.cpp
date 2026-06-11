#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_composite_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_frame_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_bloom.h"
#include "system/render/render_view.h"

namespace Engine {

GLCompositePass::GLCompositePass()
    : m_shader(std::make_unique<Core::Shader>("shaders/composite")) {}

GLCompositePass::~GLCompositePass() = default;

void GLCompositePass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view = ctx.view;

    // Back to the backbuffer, into the window's viewport rect.
    // viewportY arrives top-left origin (window/UI convention); GL's default
    // framebuffer is bottom-left, so flip against the full surface height or the
    // blit lands mirrored off the editor's viewport panel.
    const int32_t glY = static_cast<int32_t>(view.surfaceHeight)
                      - static_cast<int32_t>(view.viewportY)
                      - static_cast<int32_t>(view.viewportHeight);
    Core::FrameBuffer::bindDefault();
    ctx.gl.setViewport(
        static_cast<int32_t>(view.viewportX),
        glY,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight)
    );
    ctx.gl.setDepthTest(false);

    m_shader->bind();
    ctx.sceneHDR.bindColor(0);
    ctx.bloom.bind(1);
    const float bloomStrength = (ctx.bloom.isReady() && ctx.view.settings.bloom)
        ? ctx.view.settings.bloomStrength : 0.0f;
    m_shader->setUniform1f("u_bloomStrength", bloomStrength);
    m_triangle.draw();
}

} // namespace Engine
