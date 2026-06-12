#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_composite_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_frame_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_ao_target.h"
#include "data/gl_bloom.h"
#include "data/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
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
    beginFullscreen(ctx.gl);  // depth test / blending / face culling off, like the other post passes

    m_shader->bind();
    ctx.sceneHDR.bindColor(0);
    ctx.bloom.bind(1);
    const float bloomStrength = (ctx.bloom.isReady() && ctx.view.settings.bloom)
        ? ctx.view.settings.bloomStrength : 0.0f;
    m_shader->setUniform1f("u_bloomStrength", bloomStrength);

    // Debug views: bind the intermediate buffers the shader samples + the
    // projection for depth linearization. Default path binds nothing extra.
    const int mode = static_cast<int>(view.settings.renderMode);
    m_shader->setUniform1i("u_renderMode", mode);
    if (mode != static_cast<int>(RenderMode::Default)) {
        ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
        ctx.sceneHDR.bindGBuffer(GLBindings::PostTextureSlots::SceneGBuffer);
        ctx.ao.bindTexture(GLBindings::PostTextureSlots::SSAO);
        ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::Atlas2D);
        m_shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    }

    m_tri.draw();
}

} // namespace Engine
