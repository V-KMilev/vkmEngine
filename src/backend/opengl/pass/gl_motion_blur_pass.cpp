#include "gl_motion_blur_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_post_scratch.h"

#include "gl_screen_triangle.h"
#include "gl_blit.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLMotionBlurPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.motionBlur.enabled && !view.modeConfig.disablePost;
}

GLMotionBlurPass::GLMotionBlurPass(ShaderHandle shader)
    : RenderPass("GLMotionBlurPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLMotionBlurPass::~GLMotionBlurPass() = default;

void GLMotionBlurPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Scratch / HDR / G-buffer are owned and resized by GLBackend.
}

void GLMotionBlurPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;

    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLMotionBlurPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }
    auto& gl      = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& scratch = *rg.resource<GLPostScratch>(RGResource::PostScratch);
    if (!hdr.isReady() || !gbuffer.isReady() || !scratch.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setBlending(false);
    ctx.setFaceCulling(false);

    scratch.bindForRender();

    shader->bind();
    shader->setUniformMatrix4fv("u_invView", glm::inverse(view.camera.view));
    shader->setUniformMatrix4fv("u_prevViewProj",
        m_havePrev ? m_prevViewProj : view.camera.viewProjection);
    shader->setUniform1f("u_strength", view.environment.motionBlur.strength);
    shader->setUniform1i("u_primed", m_havePrev ? 1 : 0);

    hdr.bindResolvedColor(0);
    gbuffer.bindPosition(1);

    m_screenTri->draw();

    Core::blitColor(scratch.fboId(), hdr.resolveFboId(),
        static_cast<int>(scratch.width()), static_cast<int>(scratch.height()),
        static_cast<int>(hdr.width()),     static_cast<int>(hdr.height()), GL_NEAREST);

    m_prevViewProj = view.camera.viewProjection;
    m_havePrev = true;

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
