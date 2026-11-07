#include "pass/light/gl_reflection_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader.h"

#include "gl_screen_triangle.h"

#include "pass/gl_frame_context.h"
#include "gl_profiler.h"
#include "convention/gl_bindings.h"
#include "storage/light/gl_ibl.h"
#include "convention/gl_mip_levels.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Post  = GLBindings::PostTextureSlots;
namespace Slots = GLBindings::ReflectionTextureSlots;

} // namespace

GLReflectionPass::GLReflectionPass()
    : m_downsample("shaders/opengl/light/reflection/downsample")
    , m_trace("shaders/opengl/light/reflection/trace")
    , m_resolve("shaders/opengl/light/reflection/resolve")
    , m_found("reflection_found", Vkm::OpenGL::renderTargetParams(1, 1, GL_R32UI, GL_RED_INTEGER)) {}

GLReflectionPass::~GLReflectionPass() = default;

void GLReflectionPass::ensureTargets(uint32_t width, uint32_t height, uint32_t step) {
    m_hits.resize((width + step - 1) / step, (height + step - 1) / step);
    // From half size, one level short of MAX_CHAIN_MIPS: level 0 of what a hit reads is the lit scene
    // itself.
    const int w = static_cast<int>(width + 1) / 2;
    const int h = static_cast<int>(height + 1) / 2;
    if (m_chain.isReady() && w == m_chain.mipWidth(0) && h == m_chain.mipHeight(0)) return;
    m_chain.create(
        w,
        h,
        mipLevelsDownToTwo(w, h, MAX_CHAIN_MIPS - 1),
        GL_RGBA16F,
        GL_LINEAR_MIPMAP_LINEAR,
        GL_LINEAR
    );
}

void GLReflectionPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    // With no sky baked the forward pass still writes the reflection weight,
    // and an environment reflection of zero: the traced one only adds.
    if (!view.settings->ssr) return;

    const int step = view.settings->ssrHalfResolution ? 2 : 1;
    ensureTargets(view.sceneWidth(), view.sceneHeight(), static_cast<uint32_t>(step));
    ctx.gl.setDepthTest(false);
    ctx.screenTri.bind();
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, Post::SCENE_DEPTH);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, Post::SCENE_GBUFFER);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::ReflectWeight, Slots::WEIGHT);
    trace(ctx, step);

    // The traced color in place of the environment's, into the chain's next link. The depth,
    // G-buffer, weight and chain are still bound from above.
    ctx.colorDst->bind(ctx.gl);
    m_resolve.bind();
    m_resolve.setUniform1f("u_maxLod",    static_cast<float>(m_chain.mipCount()));
    m_resolve.setUniform1i("u_traceStep", step);
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, Post::SCENE_COLOR);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::ReflectEnv, Slots::ENV);
    m_hits.bindTexture(GLTarget::Attachment::Color, Slots::TRACE);
    {
        PROFILE_GPU_SCOPE("Reflections/Resolve");
        ctx.screenTri.emit();
    }

    ctx.flipColor();
}

void GLReflectionPass::trace(GLFrameContext& ctx, int step) {
    const float maxLod = static_cast<float>(m_chain.mipCount());
    constexpr uint32_t TARGET = 0;  // the image unit both chain shaders write through
    constexpr uint32_t FOUND  = 1;  // the trace marks a hit through it:
                                    // shaders/opengl/light/reflection/found.glsl

    // Where each glossy pixel's ray meets the scene, and whether any did.
    m_found.clear(0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    m_found.bindImage(FOUND, GL_READ_WRITE);
    m_hits.bind(ctx.gl);
    m_trace.bind();
    m_trace.setUniform1f("u_maxDistance", MAX_DISTANCE);
    m_trace.setUniform1f("u_maxLod",      maxLod);
    m_trace.setUniform1i("u_traceStep",   step);
    {
        PROFILE_GPU_SCOPE("Reflections/Trace");
        ctx.screenTri.emit();
    }
    Vkm::OpenGL::memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Each level filtered from the one before it, the first from the lit scene, a compute dispatch a
    // level as in GLBloomPass: each over at once on a frame whose trace met nothing, as no hit reads
    // the chain then.
    PROFILE_GPU_SCOPE("Reflections/Chain");
    m_downsample.bind();
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, Slots::CHAIN);
    for (int mip = 0; mip < m_chain.mipCount(); ++mip) {
        m_downsample.setUniform1i("u_first", mip == 0 ? 1 : 0);
        if (mip > 0) m_chain.bindLevel(mip - 1, Slots::CHAIN);
        m_chain.bindImage(mip, TARGET, GL_WRITE_ONLY);
        namespace Groups = GLBindings::ComputeGroups;
        m_downsample.dispatch(
            Groups::covering(static_cast<uint32_t>(m_chain.mipWidth(mip)),  Groups::IMAGE),
            Groups::covering(static_cast<uint32_t>(m_chain.mipHeight(mip)), Groups::IMAGE)
        );
        Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    m_chain.bindSlot(Slots::CHAIN);
}

} // namespace Vkm::Engine
