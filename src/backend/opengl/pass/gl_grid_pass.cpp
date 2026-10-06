#include "pass/gl_grid_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "core/math/axes.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLGridPass::GLGridPass()
    : m_shader("shaders/grid") {}

GLGridPass::~GLGridPass() = default;

void GLGridPass::execute(GLFrameContext& ctx) {
    const RenderSettings& s = ctx.view.settings;
    if (!s.gridShown()) return;

    bindBackbufferViewport(ctx);
    ctx.gl.setDepthTest(false);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.bind();
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);

    static const char* const COLOR_NAMES[3] = {"u_axisColor[0]", "u_axisColor[1]", "u_axisColor[2]"};
    for (int axis = 0; axis < 3; ++axis) {
        const Math::AxisColor c = Math::AXIS_COLORS[axis];
        m_shader.setUniform3f(COLOR_NAMES[axis], c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
    }
    m_shader.setUniform3iv("u_axisShown", glm::ivec3(s.gridAxisX, s.gridAxisY, s.gridAxisZ));
    // By normal - ZY, XZ, XY: a plane is drawn while both its axes are on.
    const glm::ivec3 planes(
        s.gridAxisY && s.gridAxisZ,
        s.gridAxisX && s.gridAxisZ,
        s.gridAxisX && s.gridAxisY
    );
    m_shader.setUniform3iv("u_planeShown", planes);

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
