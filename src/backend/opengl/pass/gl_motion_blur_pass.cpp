#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_motion_blur_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Engine {

GLMotionBlurPass::GLMotionBlurPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/motion_blur")) {}

GLMotionBlurPass::~GLMotionBlurPass() = default;

void GLMotionBlurPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view        = ctx.view;
    const glm::mat4   viewProj     = view.camera.projection * view.camera.view;
    const glm::mat4   invViewProj  = glm::inverse(viewProj);

    // First frame, or motion blur disabled: track the camera but skip the blur
    // (so re-enabling never smears against a stale matrix).
    if (!m_havePrev || !view.settings.motionBlur) {
        m_prevViewProj = viewProj;
        m_havePrev     = true;
        return;
    }

    // Read the live HDR scene, write the blur into the scratch target, blit back
    // (same feedback-safe pattern SSR uses).
    ctx.sceneColor.bind(ctx.gl);
    ctx.gl.setDepthTest(false);
    ctx.gl.setBlending(false);
    ctx.gl.setFaceCulling(false);

    m_shader->bind();
    ctx.sceneHDR.bindColor(GLBindings::PostTextureSlots::SceneColor);
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    m_shader->setUniformMatrix4fv("u_invViewProj",  invViewProj);
    m_shader->setUniformMatrix4fv("u_prevViewProj", m_prevViewProj);
    m_shader->setUniform1f("u_intensity",   view.settings.motionBlurIntensity);
    m_shader->setUniform1f("u_maxVelocity", view.settings.motionBlurMaxVelocity);
    m_shader->setUniform1i("u_samples",     view.settings.motionBlurSamples);

    m_tri.draw();

    ctx.sceneHDR.blitColorFrom(ctx.sceneColor);

    ctx.gl.setDepthTest(true);
    m_prevViewProj = viewProj;
}

} // namespace Engine
