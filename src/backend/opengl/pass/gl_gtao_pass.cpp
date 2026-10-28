#include "pass/gl_gtao_pass.h"

#include <GL/glew.h>

#include "gl_compute_shader.h"
#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "gl_profiler.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "gl_mip_levels.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Slots = GLBindings::PostTextureSlots;

// The search's widest step, a quarter of the screen, is about level five; past that no level
// is read.
constexpr int MAX_DEPTH_MIPS = 6;

} // namespace

GLGTAOPass::GLGTAOPass()
    : m_prefilter("shaders/gtao/prefilter")
    , m_shader("shaders/gtao")
    , m_denoise("shaders/gtao/denoise")
    , m_upsample("shaders/gtao/upsample") {}

GLGTAOPass::~GLGTAOPass() = default;

void GLGTAOPass::ensureDepthMips(uint32_t width, uint32_t height) {
    if (m_depthMips.isReady() && width == m_depthMipsWidth && height == m_depthMipsHeight) return;
    m_depthMipsWidth  = width;
    m_depthMipsHeight = height;

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);

    // Nearest throughout: a sample wants the depth one texel holds, at the
    // level the step length picked, not a blend of two surfaces.
    m_depthMips.create(
        w,
        h,
        mipLevelsDownToTwo(w, h, MAX_DEPTH_MIPS),
        GL_R32F,
        GL_NEAREST_MIPMAP_NEAREST,
        GL_NEAREST
    );
}

void GLGTAOPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.settings.gtao) return;

    const RenderView& view = ctx.view;
    ensureDepthMips(view.sceneWidth(), view.sceneHeight());

    // Readers bind the AO target behind ctx.aoReady or ctx.screenShadowReady. Once allocated it
    // stays: the toggle flips too often to thrash a full-scene target.
    ctx.ao.resize(view.sceneWidth(), view.sceneHeight());

    // Level 0 linearises the scene depth, each later level folds the one before; a dispatch a
    // level, as in GLBloomPass.
    const int mips = m_depthMips.mipCount();
    {
        PROFILE_GPU_SCOPE_NAMED("GTAO/Chain");
        m_prefilter.bind();
        m_prefilter.setUniform1f("u_radius", view.settings.gtaoRadius);
        for (int mip = 0; mip < mips; ++mip) {
            if (mip == 0) ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, Slots::AO_DEPTH);
            else          m_depthMips.bindLevel(mip - 1, Slots::AO_DEPTH);
            m_prefilter.setUniform1i("u_level", mip);
            m_depthMips.bindImage(mip, 0, GL_WRITE_ONLY);
            namespace Groups = GLBindings::ComputeGroups;
            m_prefilter.dispatch(
                Groups::covering(static_cast<uint32_t>(m_depthMips.mipWidth(mip)),  Groups::IMAGE),
                Groups::covering(static_cast<uint32_t>(m_depthMips.mipHeight(mip)), Groups::IMAGE)
            );
            Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        }
    }

    ctx.gl.setDepthTest(false);
    ctx.screenTri.bind();

    // At half size, into the raw target while sampling the chain and G-buffer; the upsample below
    // writes the frame's.
    const uint32_t halfWidth  = (view.sceneWidth() + 1) / 2;
    const uint32_t halfHeight = (view.sceneHeight() + 1) / 2;
    m_raw.resize(halfWidth, halfHeight);
    m_blurred.resize(halfWidth, halfHeight);
    m_raw.bind(ctx.gl);

    m_shader.bind();
    m_depthMips.bindSlot(Slots::AO_DEPTH);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, Slots::SCENE_GBUFFER);

    m_shader.setUniform1f("u_maxMip", static_cast<float>(mips - 1));
    m_shader.setUniform1f("u_radius", view.settings.gtaoRadius);

    {
        PROFILE_GPU_SCOPE_NAMED("GTAO/Search");
        ctx.screenTri.emit();
    }

    // The raw result averaged on its own surface and shaped, then widened into the frame's AO
    // target. The depth chain is still bound; its level 0 is the scene's depth.
    namespace Groups = GLBindings::ComputeGroups;
    {
        PROFILE_GPU_SCOPE_NAMED("GTAO/Denoise");
        m_denoise.bind();
        m_denoise.setUniform1f("u_intensity", view.settings.gtaoIntensity);
        m_denoise.setUniform1f("u_power",     view.settings.gtaoPower);
        m_raw.bindTexture(GLTarget::Attachment::Color, Slots::AO);
        m_blurred.bindImage(GLTarget::Attachment::Color, 0, GL_WRITE_ONLY);
        m_denoise.dispatch(
            Groups::covering(halfWidth,  Groups::IMAGE),
            Groups::covering(halfHeight, Groups::IMAGE)
        );
        Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    {
        PROFILE_GPU_SCOPE_NAMED("GTAO/Upsample");
        m_upsample.bind();
        m_blurred.bindTexture(GLTarget::Attachment::Color, Slots::AO);
        ctx.ao.bindImage(GLTarget::Attachment::Color, 0, GL_WRITE_ONLY);
        m_upsample.dispatch(
            Groups::covering(view.sceneWidth(),  Groups::IMAGE),
            Groups::covering(view.sceneHeight(), Groups::IMAGE)
        );
        Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    ctx.aoReady = true;
}

} // namespace Vkm::Engine
