#include "pass/gl_reflection_pass.h"

#include <GL/glew.h>

#include "gl_compute_shader.h"
#include "gl_error_handle.h"
#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"

#include "gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "storage/gl_ibl.h"
#include "gl_mip_levels.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Post  = GLBindings::PostTextureSlots;
namespace Slots = GLBindings::ReflectionTextureSlots;

} // namespace

GLReflectionPass::GLReflectionPass()
    : m_source("shaders/reflection/source")
    , m_downsample("shaders/reflection/downsample")
    , m_trace("shaders/reflection/trace")
    , m_resolve("shaders/reflection/resolve") {}

GLReflectionPass::~GLReflectionPass() = default;

void GLReflectionPass::ensureTargets(uint32_t width, uint32_t height) {
    m_hits.resize(width, height);
    if (m_chain.isReady() && width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    m_chain.create(
        w,
        h,
        mipLevelsDownToTwo(w, h, MAX_CHAIN_MIPS),
        GL_RGBA16F,
        GL_LINEAR_MIPMAP_LINEAR,
        GL_LINEAR
    );
}

void GLReflectionPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    // With no sky baked the forward pass still writes the reflection weight,
    // and an environment reflection of zero: the traced one only adds.
    if (!view.settings.ssr) return;

    ensureTargets(view.viewportWidth, view.viewportHeight);
    ctx.gl.setDepthTest(false);
    ctx.screenTri.bind();

    const float maxLod = static_cast<float>(m_chain.mipCount() - 1);

    // The lit scene into the chain's base, then each level filtered from the one before it,
    // a compute dispatch a level as in GLBloomPass.
    constexpr uint32_t TARGET = 0;  // the image unit both shaders write through
    const auto dispatchLevel = [&](const Vkm::GL::ComputeShader& shader, int mip) {
        m_chain.bindImage(mip, TARGET, GL_WRITE_ONLY);
        namespace Groups = GLBindings::ComputeGroups;
        shader.dispatch(
            Groups::covering(static_cast<uint32_t>(m_chain.mipWidth(mip)),  Groups::IMAGE),
            Groups::covering(static_cast<uint32_t>(m_chain.mipHeight(mip)), Groups::IMAGE)
        );
        VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
    };

    m_source.bind();
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, Post::SCENE_COLOR);
    dispatchLevel(m_source, 0);

    m_downsample.bind();
    for (int mip = 1; mip < m_chain.mipCount(); ++mip) {
        m_downsample.setUniform1i("u_first", mip == 1 ? 1 : 0);
        m_chain.bindLevel(mip - 1, Slots::CHAIN);
        dispatchLevel(m_downsample, mip);
    }
    m_chain.bindSlot(Slots::CHAIN);

    // Where each glossy pixel's ray meets the scene.
    m_hits.bind(ctx.gl);
    m_trace.bind();
    m_trace.setUniform1f("u_maxRoughness", view.settings.ssrMaxRoughness);
    m_trace.setUniform1f("u_maxDistance",  view.settings.ssrMaxDistance);
    m_trace.setUniform1f("u_maxLod",       maxLod);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, Post::SCENE_DEPTH);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, Post::SCENE_GBUFFER);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::ReflectWeight, Slots::WEIGHT);
    ctx.screenTri.emit();

    // The traced colour in place of the environment's, into the chain's next
    // link. The depth, G-buffer, weight and chain are still bound from above.
    ctx.colorDst->bind(ctx.gl);
    m_resolve.bind();
    m_resolve.setUniform1f("u_maxRoughness", view.settings.ssrMaxRoughness);
    m_resolve.setUniform1f("u_maxLod",       maxLod);
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, Post::SCENE_COLOR);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::ReflectEnv, Slots::ENV);
    m_hits.bindTexture(GLTarget::Attachment::Color, Slots::TRACE);
    ctx.screenTri.emit();

    ctx.flipColor();
}

} // namespace Vkm::Engine
