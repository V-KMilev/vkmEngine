#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_contact_shadow_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "gl_mask_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Engine {

GLContactShadowPass::GLContactShadowPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/contact_shadow")) {}

GLContactShadowPass::~GLContactShadowPass() = default;

void GLContactShadowPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    // No real sun means the mask would march for a light the forward pass never
    // applies it to - skip the whole raymarch.
    if (!view.settings.contactShadows || !ctx.hasSun) return;

    // Sun direction in view space (the mask marches in view space).
    const glm::vec3 sunDirView = glm::normalize(glm::mat3(view.camera.view) * ctx.sunDir);

    // Render the visibility mask into its own target while sampling the resolved
    // depth (a different FBO, so no read-while-write feedback).
    ctx.contactShadow.bind(ctx.gl);
    beginFullscreen(ctx.gl);

    m_shader->bind();
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);

    m_shader->setUniformMatrix4fv("u_projection",    view.camera.projection);
    m_shader->setUniformMatrix4fv("u_invProjection", view.camera.invProjection);
    m_shader->setUniform3fv("u_sunDirView", sunDirView);
    m_shader->setUniform1f("u_length",    view.settings.contactShadowLength);
    m_shader->setUniform1f("u_thickness", view.settings.contactShadowThickness);

    ctx.screenTri.draw();

    endFullscreen(ctx.gl);
    ctx.contactShadowReady = true;
}

} // namespace Engine
