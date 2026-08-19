#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_fog_apply_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "data/gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_fog_volume.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Engine {

GLFogApplyPass::GLFogApplyPass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/fog/apply")) {}

GLFogApplyPass::~GLFogApplyPass() = default;

void GLFogApplyPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (!ctx.fogReady) return;

    // Into the free scratch while sampling the current colour, then flip the
    // chain.
    ctx.colorDst->bind(ctx.gl);
    beginFullscreen(ctx.gl);

    m_shader->bind();
    ctx.colorSrc->bindColor(GLBindings::PostTextureSlots::SceneColor);
    ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
    ctx.fog.bindIntegratedSlot(GLBindings::PostTextureSlots::FogVolume);

    m_shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    m_shader->setUniform1f("u_zNear", view.camera.zNear);
    m_shader->setUniform1f("u_zFar",  view.camera.zFar);

    ctx.screenTri.draw();

    ctx.flipColor();
    endFullscreen(ctx.gl);
}

} // namespace Engine
