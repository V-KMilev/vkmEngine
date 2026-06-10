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
    if (!isEnabled()) return;

    const RenderView& view = ctx.view;

    // Render "scene + reflections" into the scratch target while sampling the
    // live scene's colour, depth, and G-buffer. Drawing into a different FBO
    // than the one we sample avoids any read-while-write feedback.
    ctx.sceneColor.bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(false);

    m_shader->bind();
    ctx.sceneHDR.bindColor(GLBindings::PostTextureSlots::SceneColor);
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    ctx.sceneHDR.bindGBuffer(GLBindings::PostTextureSlots::SceneGBuffer);

    const glm::mat4 proj    = view.camera.projection;
    const glm::mat4 invProj = glm::inverse(proj);
    m_shader->setUniformMatrix4fv("u_projection",    proj);
    m_shader->setUniformMatrix4fv("u_invProjection", invProj);
    m_shader->setUniform2f("u_screenSize",
        static_cast<float>(view.viewportWidth), static_cast<float>(view.viewportHeight));

    m_tri.draw();

    // Resolve back into the HDR scene so bloom + composite see the reflections.
    ctx.sceneHDR.blitColorFrom(ctx.sceneColor);

    ctx.gl.setDepthTest(true);
}

} // namespace Engine
