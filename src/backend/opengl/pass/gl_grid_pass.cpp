#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_grid_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "data/gl_mesh.h"
#include "generator/mesh_generators.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {

// Quad half-size in world units. The vertex shader recentres the quad on the
// camera and scales it by this, so it always covers the visible ground. The
// fragment shader's distance fade (which keys off the same extent) hides the
// quad rim, so the grid never shows a hard edge regardless of camera height.
constexpr float GRID_EXTENT = 200.0f;

} // namespace

GLGridPass::GLGridPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/grid"))
    , m_quad(std::make_unique<GLMesh>(generatePlane(2.0f, 2.0f))) {}

GLGridPass::~GLGridPass() = default;

void GLGridPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.settings.grid) return;

    const RenderView& view = ctx.view;
    const glm::mat4& viewProj = view.camera.viewProjection;

    // Alpha-blend over the current scene colour. The chain scratches carry no
    // depth attachment, so occlusion moves into the shader: it samples the
    // geometry target's depth and discards covered fragments (LEQUAL). If the
    // chain is still on the geometry target (every post pass disabled), promote
    // first - drawing into it while sampling its own depth would be a
    // read-while-write feedback.
    if (ctx.colorSrc == &ctx.sceneHDR) {
        ctx.colorDst->blitColorFrom(*ctx.colorSrc);
        ctx.flipColor();
    }
    ctx.colorSrc->bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->bind();
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    m_shader->setUniformMatrix4fv("u_viewProj", viewProj);
    m_shader->setUniform3fv("u_camPos", view.camera.position);
    m_shader->setUniform1f("u_extent", GRID_EXTENT);

    m_quad->draw();

    // Restore the engine-default depth/blend state so nothing downstream
    // inherits this pass's no-test, blended overlay setup.
    ctx.gl.setBlending(false);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LEQUAL);
}

} // namespace Engine
