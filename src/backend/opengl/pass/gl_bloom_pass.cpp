#include "gl_bloom_pass.h"

#include <GL/glew.h>

#include "logger.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"
#include "gl_screen_triangle.h"
#include "resource/gl_bloom.h"
#include "resource/gl_shader_program.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
    constexpr float UPSAMPLE_RADIUS = 0.005f;  // tent filter radius in UV space
}

bool GLBloomPass::enabledForView(const RenderView& view) const {
    // Skip when strength is zero (no visible contribution) or in wireframe.
    return isEnabled()
        && view.environment.bloom.strength > 0.0001f
        && !view.modeConfig.disablePost;
}

GLBloomPass::GLBloomPass(ShaderHandle downsampleShader, ShaderHandle upsampleShader)
    : RenderPass("GLBloomPass")
    , m_downShader(downsampleShader)
    , m_upShader(upsampleShader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLBloomPass::~GLBloomPass() = default;

void GLBloomPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLBloom is owned and resized by GLBackend.
}

void GLBloomPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLBloomPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl    = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& bloom = *rg.resource<GLBloom>(RGResource::BloomChain);
    if (!hdr.isReady() || !bloom.isReady()) return;

    GLShader* down = gl.getView().resolveShader(m_downShader, resources);
    GLShader* up   = gl.getView().resolveShader(m_upShader, resources);
    if (!down || !up) return;

    // SceneHDRResolved (the bloom source) is produced by the graph's auto
    // MSAA-resolve before this pass runs.

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setFaceCulling(false);
    ctx.setBlending(false);

    m_screenTri->bind();
    bloom.bindFbo();

    const int mips = bloom.mipCount();

    // Progressive downsample: resolved HDR -> mip 0 -> ... -> mip N-1.
    down->bind();
    for (int mip = 0; mip < mips; ++mip) {
        if (mip == 0) {
            hdr.bindResolvedColor(0);
            down->setUniform1f("u_srcLod", 0.0f);
            down->setUniform1i("u_karis", 1);
        } else {
            bloom.bind(0);
            down->setUniform1f("u_srcLod", static_cast<float>(mip - 1));
            down->setUniform1i("u_karis", 0);
        }
        bloom.attachMip(mip);
        m_screenTri->emit();
    }

    // Additive upsample back up the chain (tent filter).
    up->bind();
    up->setUniform1f("u_filterRadius", UPSAMPLE_RADIUS);
    bloom.bind(0);
    ctx.setBlending(true);
    ctx.setBlendFunc(GL_ONE, GL_ONE);
    for (int mip = mips - 1; mip > 0; --mip) {
        up->setUniform1f("u_srcLod", static_cast<float>(mip));
        bloom.attachMip(mip - 1);
        m_screenTri->emit();
    }
    ctx.setBlending(false);

    m_screenTri->unbind();
    bloom.unbindFbo();

    // Composite restores its own state; leave depth enabled for the next
    // frame's scene passes (they rely on the context default).
    ctx.setDepthTest(true);
}

} // namespace Engine
