#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_hiz_pass.h"

#include <GL/glew.h>

#include "logger.h"

#include "core/gl_backend.h"
#include "debug/profiler_gl.h"
#include "gl_screen_triangle.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_hiz.h"
#include "resource/gl_shader_program.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

bool GLHiZPass::enabledForView(const RenderView& view) const {
    // Off by default - the pyramid has no consumer in this commit, so
    // it would only be wasted GPU work. The user toggles it on once a
    // future occlusion pass starts reading it.
    return isEnabled()
        && view.environment.occlusion.useHiZ
        && !view.modeConfig.disablePost;
}

GLHiZPass::GLHiZPass(ShaderHandle initShader, ShaderHandle reduceShader)
    : RenderPass("GLHiZPass")
    , m_initShader(initShader)
    , m_reduceShader(reduceShader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLHiZPass::~GLHiZPass() = default;

void GLHiZPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLHiZ is owned and resized by FrameResources.
}

void GLHiZPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) return;

    auto& gl       = static_cast<GLBackend&>(backend);
    auto& gbuffer  = *rg.resource<GLGBuffer>(RGResource::GBufferPosition);
    auto& hiz      = *rg.resource<GLHiZ>(RGResource::HiZPyramid);
    if (!gbuffer.isReady() || !hiz.isReady()) return;

    GLShader* init   = gl.getView().resolveShader(m_initShader,   resources);
    GLShader* reduce = gl.getView().resolveShader(m_reduceShader, resources);
    if (!init || !reduce) return;

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setFaceCulling(false);
    ctx.setBlending(false);

    m_screenTri->bind();
    hiz.bindFbo();

    // Mip 0: project view-space position's Z into the pyramid.
    init->bind();
    gbuffer.bindPosition(0);
    if (init->hasUniform("u_viewPos")) init->setUniform1i("u_viewPos", 0);
    hiz.attachMip(0);
    m_screenTri->emit();

    // Mips 1..N-1: max(2x2) of the level below.
    reduce->bind();
    hiz.bind(0);
    if (reduce->hasUniform("u_src")) reduce->setUniform1i("u_src", 0);
    const int mips = hiz.mipCount();
    for (int mip = 1; mip < mips; ++mip) {
        reduce->setUniform1f("u_srcLod", static_cast<float>(mip - 1));
        hiz.attachMip(mip);
        m_screenTri->emit();
    }

    m_screenTri->unbind();
    hiz.unbindFbo();

    ctx.setDepthTest(true);
}

} // namespace Engine
