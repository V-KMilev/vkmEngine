#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_bloom_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "data/gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "gl_target.h"
#include "data/gl_bloom.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLBloomPass::GLBloomPass()
    : m_down(std::make_unique<Vkm::GL::Shader>("shaders/bloom/down"))
    , m_up(std::make_unique<Vkm::GL::Shader>("shaders/bloom/up")) {}

GLBloomPass::~GLBloomPass() = default;

void GLBloomPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.settings.bloom) return;

    GLBloom& bloom = ctx.bloom;
    if (!bloom.isReady()) return;

    const RenderSettings& settings = ctx.view.settings;

    beginFullscreen(ctx.gl);

    ctx.screenTri.bind();
    bloom.bindFbo();

    const int mips = bloom.mipCount();

    // The first tap soft-knee prefilters + Karis-averages; the rest are plain
    // 13-tap.
    m_down->bind();
    for (int mip = 0; mip < mips; ++mip) {
        if (mip == 0) {
            ctx.colorSrc->bindColor(GLBindings::BloomTextureSlots::Source);
            m_down->setUniform1f("u_srcLod", 0.0f);
            m_down->setUniform1i("u_karis", 1);
            m_down->setUniform1f("u_threshold", settings.bloomThreshold);
            m_down->setUniform1f("u_knee",      settings.bloomKnee);
        } else {
            bloom.bind(GLBindings::BloomTextureSlots::Source);
            m_down->setUniform1f("u_srcLod", static_cast<float>(mip - 1));
            m_down->setUniform1i("u_karis", 0);
        }
        bloom.attachMip(mip);
        ctx.screenTri.emit();
    }

    m_up->bind();
    m_up->setUniform1f("u_filterRadius", settings.bloomRadius);
    bloom.bind(GLBindings::BloomTextureSlots::Source);
    ctx.gl.setBlending(true);
    ctx.gl.setBlendFunc(GL_ONE, GL_ONE);
    for (int mip = mips - 1; mip > 0; --mip) {
        m_up->setUniform1f("u_srcLod", static_cast<float>(mip));
        bloom.attachMip(mip - 1);
        ctx.screenTri.emit();
    }
    ctx.gl.setBlending(false);

    ctx.screenTri.unbind();
    bloom.unbindFbo();
    endFullscreen(ctx.gl);
}

} // namespace Vkm::Engine
