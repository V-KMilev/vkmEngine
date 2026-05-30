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
    return isEnabled()
        && view.environment.occlusion.useHiZ
        && !view.modeConfig.disablePost;
}

GLHiZPass::GLHiZPass(ShaderHandle initShader, ShaderHandle reduceShader)
    : GLRenderPass("GLHiZPass")
    , m_initShader(initShader)
    , m_reduceShader(reduceShader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLHiZPass::~GLHiZPass() = default;

void GLHiZPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // GLHiZ is owned and resized by FrameResources.
}

void GLHiZPass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const ResourceManager& resources = rg.resources;
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

    // CPU-side readback of one mid mip so the next frame's visibility
    // system can AABB-test against it. Synchronous glReadPixels stalls
    // briefly; a PBO double-buffer would drop the stall but is heavier.
    // Mip 4 is 1/16th the viewport resolution - coarse enough to fit
    // in a single readback, fine enough to discriminate object-sized
    // AABBs.
    constexpr int kReadbackMip = 4;
    const int readbackMip = kReadbackMip < mips ? kReadbackMip : mips - 1;
    const int rw = hiz.mipWidth(readbackMip);
    const int rh = hiz.mipHeight(readbackMip);
    std::vector<float> cpu(static_cast<std::size_t>(rw) * rh);

    // Bind the FBO that owns the chosen mip as the read source; mip 0's
    // pyramid FBO array entry is what attachMip routes to per call, so
    // we re-issue it here to position the read.
    hiz.bindFbo();
    hiz.attachMip(readbackMip);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0,
                 static_cast<GLsizei>(rw), static_cast<GLsizei>(rh),
                 GL_RED, GL_FLOAT, cpu.data());
    hiz.unbindFbo();

    // viewProj = projection * view (column-major glm convention; matches
    // the rest of the engine's matrix math). The view matrix is published
    // alongside so the AABB test can compute distance-from-camera without
    // a separate projection-inverse.
    const glm::mat4 viewProj = rg.view.camera.projection * rg.view.camera.view;
    gl.publishOcclusion(std::move(cpu),
        static_cast<std::uint32_t>(rw),
        static_cast<std::uint32_t>(rh),
        rg.view.camera.view,
        viewProj);
}

} // namespace Engine
