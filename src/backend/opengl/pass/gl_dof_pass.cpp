#include "pass/gl_dof_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLDoFPass::GLDoFPass()
    : m_shader("shaders/dof") {}

GLDoFPass::~GLDoFPass() = default;

void GLDoFPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.camera.dofAmount <= 0.0f) return;

    // Into the free scratch while sampling the current colour, then flip the chain.
    ctx.colorDst->bind(ctx.gl);
    ctx.gl.setDepthTest(false);

    m_shader.bind();
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);

    m_shader.setUniform1f("u_focusDistance", view.camera.focusDistance);
    m_shader.setUniform1f("u_amount",        view.camera.dofAmount);
    // A share of the picture's height, so a lens looks the same at 1080p and at 4K.
    m_shader.setUniform1f("u_maxRadius", view.camera.dofMaxBlur * static_cast<float>(view.viewportHeight));

    ctx.screenTri.draw();

    ctx.flipColor();
}

} // namespace Vkm::Engine
