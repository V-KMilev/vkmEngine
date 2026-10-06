#include "pass/gl_bloom_pass.h"

#include <GL/glew.h>

#include "gl_compute_shader.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "gl_target.h"
#include "storage/gl_bloom.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLBloomPass::GLBloomPass()
    : m_down("shaders/bloom/down")
    , m_up("shaders/bloom/up") {}

GLBloomPass::~GLBloomPass() = default;

void GLBloomPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.settings.bloom) return;

    GLBloom& bloom = ctx.bloom;
    if (!bloom.isReady()) return;

    const RenderSettings& settings = ctx.view.settings;

    constexpr uint32_t SOURCE = GLBindings::BloomTextureSlots::SOURCE;
    constexpr uint32_t TARGET = 0;  // the image unit both shaders write through
    const int mips = bloom.mipCount();

    // A level written as an image is read next through a sampler, or through
    // the image again by an upsample adding into it.
    const auto dispatchLevel = [&](const Vkm::GL::ComputeShader& shader, int mip) {
        namespace Groups = GLBindings::ComputeGroups;
        shader.dispatch(
            Groups::covering(static_cast<uint32_t>(bloom.mipWidth(mip)),  Groups::IMAGE),
            Groups::covering(static_cast<uint32_t>(bloom.mipHeight(mip)), Groups::IMAGE)
        );
        VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT));
    };

    // Down: the first tap soft-knee prefilters and Karis-averages the scene; the rest are plain
    // 13-tap, each reading the level above it.
    m_down.bind();
    m_down.setUniform1f("u_threshold", settings.bloomThreshold);
    m_down.setUniform1f("u_knee",      settings.bloomKnee);
    for (int mip = 0; mip < mips; ++mip) {
        if (mip == 0) ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, SOURCE);
        else          bloom.bindLevel(mip - 1, SOURCE);
        m_down.setUniform1i("u_karis", mip == 0 ? 1 : 0);
        bloom.bindImage(mip, TARGET, GL_WRITE_ONLY);
        dispatchLevel(m_down, mip);
    }

    // Up: each level is tent-filtered and added into the one below it.
    m_up.bind();
    m_up.setUniform1f("u_filterRadius", settings.bloomRadius);
    for (int mip = mips - 1; mip > 0; --mip) {
        bloom.bindLevel(mip, SOURCE);
        bloom.bindImage(mip - 1, TARGET, GL_READ_WRITE);
        dispatchLevel(m_up, mip - 1);
    }

    ctx.bloomReady = true;
}

} // namespace Vkm::Engine
