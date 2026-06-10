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

namespace {
// Motion-blur tuning. Kept as constants (no settings UI on the new pipeline
// yet); promote to the scene/engine config if these need authoring.
constexpr float MB_INTENSITY    = 1.0f;   ///< Velocity scale (0 = off).
constexpr float MB_MAX_VELOCITY = 0.05f;  ///< Clamp on the per-pixel smear (UV).
constexpr int   MB_SAMPLES      = 8;      ///< Taps along the velocity vector.
}

GLMotionBlurPass::GLMotionBlurPass()
    : m_shader(std::make_unique<Core::Shader>("shaders/motion_blur")) {}

GLMotionBlurPass::~GLMotionBlurPass() = default;

void GLMotionBlurPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    const RenderView& view        = ctx.view;
    const glm::mat4   viewProj     = view.camera.projection * view.camera.view;
    const glm::mat4   invViewProj  = glm::inverse(viewProj);

    // First frame: no previous camera to reproject against. Seed it and skip the
    // blur so we never smear against a garbage matrix.
    if (!m_havePrev) {
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
    m_shader->setUniform2f("u_screenSize",
        static_cast<float>(view.viewportWidth), static_cast<float>(view.viewportHeight));
    m_shader->setUniform1f("u_intensity",   MB_INTENSITY);
    m_shader->setUniform1f("u_maxVelocity", MB_MAX_VELOCITY);
    m_shader->setUniform1i("u_samples",     MB_SAMPLES);

    m_tri.draw();

    ctx.sceneHDR.blitColorFrom(ctx.sceneColor);

    ctx.gl.setDepthTest(true);
    m_prevViewProj = viewProj;
}

} // namespace Engine
