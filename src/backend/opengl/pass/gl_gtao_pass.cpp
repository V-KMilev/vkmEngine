#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_gtao_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_ao_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Engine {

GLGTAOPass::GLGTAOPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/gtao")) {}

GLGTAOPass::~GLGTAOPass() = default;

void GLGTAOPass::execute(GLFrameContext& ctx) {
    if (!isEnabled() || !ctx.view.settings.gtao) return;

    const RenderView& view = ctx.view;

    // Render the AO factor into its own target while sampling the scene depth +
    // G-buffer (a different FBO, so no read-while-write feedback).
    ctx.ao.bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);

    m_shader->bind();
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    ctx.sceneHDR.bindGBuffer(GLBindings::PostTextureSlots::SceneGBuffer);

    const glm::mat4 proj = view.camera.projection;
    m_shader->setUniformMatrix4fv("u_invProjection", glm::inverse(proj));
    m_shader->setUniform1f("u_proj11",    proj[1][1]);
    m_shader->setUniform1f("u_radius",    view.settings.gtaoRadius);
    m_shader->setUniform1f("u_intensity", view.settings.gtaoIntensity);
    m_shader->setUniform1f("u_power",     view.settings.gtaoPower);
    m_shader->setUniform1f("u_bias",      view.settings.gtaoBias);

    m_tri.draw();

    ctx.gl.setDepthTest(true);
    ctx.aoReady = true;
}

} // namespace Engine
