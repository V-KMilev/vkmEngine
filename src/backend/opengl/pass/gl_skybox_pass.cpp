#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_skybox_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_ibl.h"
#include "data/gl_mesh.h"
#include "convention/gl_bindings.h"
#include "generator/mesh_generators.h"
#include "system/render/render_view.h"

namespace Engine {

GLSkyboxPass::GLSkyboxPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/skybox"))
    , m_cube(std::make_unique<GLMesh>(generateCube())) {}

GLSkyboxPass::~GLSkyboxPass() = default;

void GLSkyboxPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;
    if (!ctx.ibl.isReady()) return;  // no baked environment -> nothing to draw

    const RenderView& view = ctx.view;

    // Draw into the same HDR target as the forward pass (no clear). The vertex
    // shader forces z = w so the cube sits at the far plane; depth func LEQUAL
    // with depth writes off fills only the background pixels (depth == far from
    // the prepass) and leaves lit geometry untouched.
    ctx.sceneHDR.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);   // viewed from inside the cube

    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_view", view.camera.view);
    m_shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    m_shader->setUniform1f("u_iblIntensity", 1.0f);

    ctx.ibl.bindEnvCube(GLBindings::IBLTextureSlots::EnvCube);
    m_cube->draw();

    // The forward/prepass set their own depth state each frame, but restore the
    // default compare + writes so nothing downstream inherits LEQUAL/no-write.
    ctx.gl.setDepthFunc(GL_LESS);
    ctx.gl.setDepthWrite(true);
}

} // namespace Engine
