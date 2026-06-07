#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_hiz_pass.h"

#include <cstddef>
#include <cstring>
#include <vector>

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

GLHiZPass::~GLHiZPass() {
    if (m_pbo[0]) glDeleteBuffers(PBO_RING, m_pbo);
}

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

    // Asynchronous CPU readback of one mid mip so the next frame's visibility
    // system can AABB-test against it. glReadPixels into a pixel-pack buffer
    // returns immediately - the DMA runs in the background instead of stalling
    // the pipeline - and we map the OTHER ring buffer, filled last frame and so
    // already resident, to publish. Net: occlusion is one extra frame late (the
    // OcclusionOracle already tolerates one-frame latency), with no per-frame
    // CPU<->GPU sync. Mip 4 is 1/16th the viewport resolution - coarse enough to
    // fit one readback, fine enough to discriminate object-sized AABBs.
    constexpr int kReadbackMip = 4;
    const int readbackMip = kReadbackMip < mips ? kReadbackMip : mips - 1;
    const int rw = hiz.mipWidth(readbackMip);
    const int rh = hiz.mipHeight(readbackMip);
    const std::size_t bytes = static_cast<std::size_t>(rw) * rh * sizeof(float);

    // (Re)allocate the ring when the readback size changes; buffers filled at a
    // different size no longer match, so drop their validity.
    if (rw != m_pboW || rh != m_pboH) {
        if (!m_pbo[0]) glGenBuffers(PBO_RING, m_pbo);
        for (int i = 0; i < PBO_RING; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes),
                nullptr, GL_STREAM_READ);
            m_pboValid[i] = false;
        }
        m_pboW = rw;
        m_pboH = rh;
    }

    const int cur  = m_pboIndex;
    const int prev = (m_pboIndex + 1) % PBO_RING;

    // viewProj = projection * view (column-major glm convention). Stored next to
    // each buffer so the published depth and the matrices it was rendered with
    // stay in lockstep even though we publish a frame late.
    const glm::mat4 viewProj = rg.view.camera.projection * rg.view.camera.view;

    // Kick off this frame's readback into the current buffer (returns at once).
    hiz.bindFbo();
    hiz.attachMip(readbackMip);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[cur]);
    glReadPixels(0, 0,
                 static_cast<GLsizei>(rw), static_cast<GLsizei>(rh),
                 GL_RED, GL_FLOAT, nullptr);
    m_pboValid[cur]    = true;
    m_pboView[cur]     = rg.view.camera.view;
    m_pboViewProj[cur] = viewProj;

    // Map the buffer filled last frame - its transfer has long since finished,
    // so the map does not block - and hand it to the oracle.
    if (m_pboValid[prev]) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo[prev]);
        if (const void* src = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT)) {
            std::vector<float> cpu(static_cast<std::size_t>(rw) * rh);
            std::memcpy(cpu.data(), src, bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            gl.publishOcclusion(std::move(cpu),
                static_cast<std::uint32_t>(rw),
                static_cast<std::uint32_t>(rh),
                m_pboView[prev],
                m_pboViewProj[prev]);
        }
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    hiz.unbindFbo();

    m_pboIndex = prev;  // next frame overwrites the buffer we just consumed
}

} // namespace Engine
