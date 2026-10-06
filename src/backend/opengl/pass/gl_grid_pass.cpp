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

namespace {

/// How near a view's axis must come to a world axis, as a cosine, to count as looking down it.
constexpr float ALIGNED_COS = 0.9998f;  // about a degree

/**
 * @brief The planes the grid draws, by normal (ZY, XZ, XY), for this view.
 *
 * A plane is drawn while both its axes are on. An orthographic view down a world axis
 * sees the planes along it edge-on, which shows nothing, so it takes the plane facing it
 * while any plane is on.
 *
 * @param s      The axis switches.
 * @param camera The view's camera.
 * @return 1 for each plane drawn.
 */
glm::ivec3 planesShown(const RenderSettings& s, const CameraData& camera) {
    const glm::ivec3 planes(
        s.gridAxisY && s.gridAxisZ,
        s.gridAxisX && s.gridAxisZ,
        s.gridAxisX && s.gridAxisY
    );
    const bool orthographic = camera.projection[3][3] != 0.0f;
    if (!orthographic || planes == glm::ivec3(0)) return planes;

    const glm::vec3 forward = -glm::vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    for (int axis = 0; axis < 3; ++axis) {
        if (glm::abs(forward[axis]) < ALIGNED_COS) continue;
        glm::ivec3 facing(0);
        facing[axis] = 1;
        return facing;
    }
    return planes;
}

} // namespace

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
    m_shader.setUniform3iv("u_planeShown", planesShown(s, ctx.view.camera));

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
