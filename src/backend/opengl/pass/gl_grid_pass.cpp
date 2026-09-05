#include "pass/gl_grid_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "data/gl_mesh.h"
#include "resource/generate/mesh_generators.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// The grid's base reach in world units, before the shaders scale it by how far
// the camera is from the plane. That curve lives in the two shader stages
// instead of here: the vertex stage sizes the quad by it and the fragment stage
// puts the fade inside that, so they have to agree - and a shader edit reloads
// in a second where this needs a rebuild.
constexpr float GRID_BASE_EXTENT = 100.0f;

} // namespace

GLGridPass::GLGridPass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/grid"))
    , m_quad(std::make_unique<GLMesh>(generatePlane(2.0f, 2.0f))) {}

GLGridPass::~GLGridPass() = default;

void GLGridPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.settings.grid) return;

    const RenderView& view = ctx.view;
    const glm::mat4& viewProj = view.camera.viewProjection;

    const float height = glm::max(1.0f, glm::abs(view.camera.position.y));
    const float extent = GRID_BASE_EXTENT * glm::max(1.0f, glm::log(height));

    // The chain scratches carry no depth attachment, so occlusion moves into the
    // shader: it samples the geometry target's depth and discards covered
    // fragments (LEQUAL).
    promoteColorChain(ctx);
    ctx.colorSrc->bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->bind();
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SCENE_DEPTH);
    m_shader->setUniformMatrix4fv("u_viewProj", viewProj);
    m_shader->setUniform3fv("u_camPos", view.camera.position);
    m_shader->setUniform1f("u_extent", extent);

    m_quad->draw();

    // Restore the engine-default depth/blend state so nothing downstream
    // inherits this overlay's no-test, blended setup.
    ctx.gl.setBlending(false);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(true);
    ctx.gl.setDepthFunc(GL_LEQUAL);
}

} // namespace Vkm::Engine
