#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_dof_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "data/gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
// Blur radius (pixels) at full circle of confusion.
constexpr float MAX_BLUR_RADIUS = 12.0f;
} // namespace

GLDoFPass::GLDoFPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/dof")) {}

GLDoFPass::~GLDoFPass() = default;

void GLDoFPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.camera.dofAmount <= 0.0f) return;

    // Blur into the free scratch while sampling the current scene colour + the
    // geometry target's depth, then flip the chain.
    ctx.colorDst->bind(ctx.gl);
    beginFullscreen(ctx.gl);

    m_shader->bind();
    ctx.colorSrc->bindColor(GLBindings::PostTextureSlots::SceneColor);
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);

    m_shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    m_shader->setUniform1f("u_focusDistance", view.camera.focusDistance);
    m_shader->setUniform1f("u_amount",        view.camera.dofAmount);
    m_shader->setUniform1f("u_maxRadius",     MAX_BLUR_RADIUS);
    m_shader->setUniform2f("u_texel",
                           1.0f / static_cast<float>(view.viewportWidth),
                           1.0f / static_cast<float>(view.viewportHeight));

    ctx.screenTri.draw();

    ctx.flipColor();
    endFullscreen(ctx.gl);
}

} // namespace Engine
