#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_ssr_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Engine {

GLSSRPass::GLSSRPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/ssr")) {}

GLSSRPass::~GLSSRPass() = default;

void GLSSRPass::execute(GLFrameContext& ctx) {
    if (!isEnabled() || !ctx.view.settings.ssr) return;

    const RenderView& view = ctx.view;

    // Render "scene + reflections" into the scratch target while sampling the
    // live scene's colour, depth, and G-buffer. Drawing into a different FBO
    // than the one we sample avoids any read-while-write feedback.
    ctx.sceneColor.bind(ctx.gl);
    beginFullscreen(ctx.gl);

    m_shader->bind();
    ctx.sceneHDR.bindColor(GLBindings::PostTextureSlots::SceneColor);
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    ctx.sceneHDR.bindGBuffer(GLBindings::PostTextureSlots::SceneGBuffer);

    m_shader->setUniformMatrix4fv("u_projection",    view.camera.projection);
    m_shader->setUniformMatrix4fv("u_invProjection", view.camera.invProjection);
    m_shader->setUniform1f("u_intensity",   view.settings.ssrIntensity);
    m_shader->setUniform1f("u_maxDistance", view.settings.ssrMaxDistance);

    m_tri.draw();

    // Resolve back into the HDR scene so bloom + composite see the reflections.
    ctx.sceneHDR.blitColorFrom(ctx.sceneColor);

    endFullscreen(ctx.gl);
}

} // namespace Engine
