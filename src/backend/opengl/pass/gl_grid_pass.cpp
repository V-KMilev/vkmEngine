#include "pass/gl_grid_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "asset/gl_mesh.h"
#include "resource/generate/mesh_generators.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// Base reach in world units. execute() grows it with the log of the camera's height and
// passes it as u_extent, which both stages read: one sizes the quad, the other fades inside it.
constexpr float GRID_BASE_EXTENT = 100.0f;

} // namespace

GLGridPass::GLGridPass(GLMeshPool& pool)
    : m_shader("shaders/grid")
    , m_quad(std::make_unique<GLMesh>(pool, generatePlane(2.0f, 2.0f))) {}

GLGridPass::~GLGridPass() = default;

void GLGridPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.settings.grid) return;

    const RenderView& view = ctx.view;

    const float height = glm::max(1.0f, glm::abs(view.camera.position.y));
    const float extent = GRID_BASE_EXTENT * glm::max(1.0f, glm::log(height));

    promoteColorChain(ctx);
    ctx.colorSrc->bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.bind();
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
    m_shader.setUniform1f("u_extent", extent);

    m_quad->draw();
}

} // namespace Vkm::Engine
