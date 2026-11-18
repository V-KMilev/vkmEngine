#include "pass/overlay/gl_grid_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"

#include "gl_screen_triangle.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "convention/gl_bindings.h"
#include "debug/draw/debug_palette.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr float ALIGNED_COS = 0.9998f;  ///< How near a view's axis must come to a world axis, as a cosine, to
                                        ///< count as looking down it: about a degree.

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
glm::ivec3 planesShown(const HostChrome::Display& s, const CameraData& camera) {
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
    : m_shader("shaders/opengl/overlay/grid") {}

GLGridPass::~GLGridPass() = default;

void GLGridPass::execute(GLFrameContext& ctx) {
    const HostChrome::Display& s = ctx.view.display;
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
        const DebugPalette::Color c = DebugPalette::AXES[axis];
        m_shader.setUniform3f(COLOR_NAMES[axis], c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
    }
    m_shader.setUniform3iv("u_axisShown", glm::ivec3(s.gridAxisX, s.gridAxisY, s.gridAxisZ));
    m_shader.setUniform3iv("u_planeShown", planesShown(s, ctx.view.camera));
    m_shader.setUniform1f("u_screenHeight", static_cast<float>(ctx.view.viewportHeight));

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
