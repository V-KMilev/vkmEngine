#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_bloom_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_bloom.h"

namespace Engine {

namespace {
// Bloom prefilter + upsample tuning. Bright-pass threshold/knee tame fireflies;
// the tent radius is in UV space. (Kept as constants for now; promote to the
// scene/engine config if these need authoring.)
constexpr float BLOOM_THRESHOLD = 1.0f;
constexpr float BLOOM_KNEE      = 0.5f;
constexpr float UPSAMPLE_RADIUS = 0.005f;
}

GLBloomPass::GLBloomPass()
    : m_down(std::make_unique<Core::Shader>("shaders/bloom/down"))
    , m_up(std::make_unique<Core::Shader>("shaders/bloom/up")) {}

GLBloomPass::~GLBloomPass() = default;

void GLBloomPass::execute(GLFrameContext& ctx) {
    if (!isEnabled()) return;

    GLBloom& bloom = ctx.bloom;
    if (!bloom.isReady()) return;

    ctx.gl.setDepthTest(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setBlending(false);

    m_tri.bind();
    bloom.bindFbo();

    const int mips = bloom.mipCount();

    // Progressive downsample: HDR scene -> mip 0 -> ... -> mip N-1. The first
    // tap soft-knee prefilters + Karis-averages; the rest are plain 13-tap.
    m_down->bind();
    for (int mip = 0; mip < mips; ++mip) {
        if (mip == 0) {
            ctx.sceneHDR.bindColor(0);
            m_down->setUniform1f("u_srcLod", 0.0f);
            m_down->setUniform1i("u_karis", 1);
            m_down->setUniform1f("u_threshold", BLOOM_THRESHOLD);
            m_down->setUniform1f("u_knee",      BLOOM_KNEE);
        } else {
            bloom.bind(0);
            m_down->setUniform1f("u_srcLod", static_cast<float>(mip - 1));
            m_down->setUniform1i("u_karis", 0);
        }
        bloom.attachMip(mip);
        m_tri.emit();
    }

    // Additive upsample back up the chain (tent filter).
    m_up->bind();
    m_up->setUniform1f("u_filterRadius", UPSAMPLE_RADIUS);
    bloom.bind(0);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_ONE, GL_ONE);
    for (int mip = mips - 1; mip > 0; --mip) {
        m_up->setUniform1f("u_srcLod", static_cast<float>(mip));
        bloom.attachMip(mip - 1);
        m_tri.emit();
    }
    ctx.gl.setBlending(false);

    m_tri.unbind();
    bloom.unbindFbo();
    ctx.gl.setDepthTest(true);  // leave the default for the next frame's scene passes
}

} // namespace Engine
