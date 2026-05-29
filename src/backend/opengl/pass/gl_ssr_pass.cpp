#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_ssr_pass.h"

#include <GL/glew.h>

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

bool GLSSRPass::enabledForView(const RenderView& view) const {
    // Off in wireframe: SSR reads filled-triangle gbuffer positions and
    // would draw a ghost of the solid mesh between wireframe lines.
    return isEnabled() && view.environment.ssr.enabled && !view.modeConfig.disablePost;
}

GLSSRPass::GLSSRPass(ShaderHandle shader)
    : RenderPass("GLSSRPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLSSRPass::~GLSSRPass() = default;

void GLSSRPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Reuses GLGBuffer / GLSceneTarget, both owned and resized by GLBackend.
}

void GLSSRPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLSSRPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }
    auto& gl      = static_cast<GLBackend&>(backend);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& hdr     = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& scratch = *rg.resource<GLPostScratch>(RGResource::PostScratch);
    if (!gbuffer.isReady() || !hdr.isReady() || !scratch.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setBlending(false);
    ctx.setFaceCulling(false);

    // Render scene + reflections into the scratch target, then blit back into
    // the resolved HDR (DoF / motion-blur pattern). This keeps SSR off the 4x
    // MSAA target and lets the graph resolve once instead of re-resolving for
    // the next reader. The graph resolved SceneHDRResolved (the sample source)
    // before this pass ran; the shader passes the scene through and adds the
    // reflection on top, so no blend is needed.
    scratch.bindForRender();

    shader->bind();
    shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    shader->setUniform1f("u_intensity",   view.environment.ssr.intensity);
    shader->setUniform1f("u_maxDistance", view.environment.ssr.maxDistance);
    shader->setUniform1f("u_thickness",   view.environment.ssr.thickness);

    hdr.bindResolvedColor(0);
    gbuffer.bindNormal(1);
    gbuffer.bindPosition(2);

    m_screenTri->draw();

    Core::blitColor(scratch.fboId(), hdr.resolveFboId(),
        static_cast<int>(scratch.width()), static_cast<int>(scratch.height()),
        static_cast<int>(hdr.width()),     static_cast<int>(hdr.height()), GL_NEAREST);

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
