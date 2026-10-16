#include "pass/gl_reflection_pass.h"

#include <algorithm>

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
#include "pass/gl_planar_reflection_pass.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

namespace Post  = GLBindings::PostTextureSlots;
namespace Slots = GLBindings::ReflectionTextureSlots;

/// One planar reflector's uniform names and picture slot: the trace takes its place, the resolve that
/// and its picture.
struct PlanarUniforms {
    const char* plane;
    const char* center;
    const char* axisU;
    const char* axisV;
    const char* lod;
    uint32_t    slot;
};
constexpr PlanarUniforms PLANAR_UNIFORMS[GLPlanarReflectionPass::MAX_DRAWN] = {
    {
        "u_planarPlane[0]",
        "u_planarCenter[0]",
        "u_planarU[0]",
        "u_planarV[0]",
        "u_planarLod[0]",
        Slots::PLANAR_0
    },
    {
        "u_planarPlane[1]",
        "u_planarCenter[1]",
        "u_planarU[1]",
        "u_planarV[1]",
        "u_planarLod[1]",
        Slots::PLANAR_1
    },
};

} // namespace

GLReflectionPass::GLReflectionPass(const GLPlanarReflectionPass& planar)
    : m_source("shaders/reflection/source")
    , m_downsample("shaders/reflection/downsample")
    , m_trace("shaders/reflection/trace")
    , m_resolve("shaders/reflection/resolve")
    , m_planar(planar) {}

GLReflectionPass::~GLReflectionPass() = default;

void GLReflectionPass::ensureTargets(uint32_t width, uint32_t height, uint32_t step) {
    m_hits.resize((width + step - 1) / step, (height + step - 1) / step);
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
    const bool traced = view.settings.ssr;
    if (!traced && m_planar.count() == 0) return;

    const int step = view.settings.ssrHalfResolution ? 2 : 1;
    ensureTargets(view.sceneWidth(), view.sceneHeight(), static_cast<uint32_t>(step));
    ctx.gl.setDepthTest(false);
    ctx.screenTri.bind();
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, Post::SCENE_DEPTH);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, Post::SCENE_GBUFFER);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::ReflectWeight, Slots::WEIGHT);
    if (traced) trace(ctx, step);

    // The traced colour in place of the environment's, into the chain's next
    // link. The depth, G-buffer, weight and chain are still bound from above.
    ctx.colorDst->bind(ctx.gl);
    m_resolve.bind();
    m_resolve.setUniform1f("u_maxRoughness", view.settings.ssrMaxRoughness);
    m_resolve.setUniform1f("u_maxLod",       static_cast<float>(m_chain.mipCount() - 1));
    m_resolve.setUniform1i("u_traceStep",    step);
    m_resolve.setUniform1i("u_traced",       traced ? 1 : 0);
    bindPlanar(ctx);
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, Post::SCENE_COLOR);
    ctx.sceneHDR.bindTexture(GLTarget::Attachment::ReflectEnv, Slots::ENV);
    m_hits.bindTexture(GLTarget::Attachment::Color, Slots::TRACE);
    ctx.screenTri.emit();

    ctx.flipColor();
}

void GLReflectionPass::bindPlanar(GLFrameContext& ctx) {
    // Each reflector in view space, its rectangle's axes over its half-extents, so a pixel on it
    // reads within one along each.
    const glm::mat4& toView = ctx.view.camera.view;
    const int count = m_planar.count();
    m_resolve.setUniform1i("u_planarCount", count);
    for (int i = 0; i < count; ++i) {
        const PlanarReflectorData& r = m_planar.reflector(i);
        const glm::vec3 center = glm::vec3(toView * glm::vec4(r.center, 1.0f));
        const glm::vec3 normal = glm::normalize(glm::mat3(toView) * r.normal);
        const glm::vec3 axisU  = glm::mat3(toView) * r.axisU / std::max(r.halfExtents.x, 1e-4f);
        const glm::vec3 axisV  = glm::mat3(toView) * r.axisV / std::max(r.halfExtents.y, 1e-4f);
        const PlanarUniforms& names = PLANAR_UNIFORMS[i];
        m_resolve.setUniform4fv(names.plane, glm::vec4(normal, -glm::dot(normal, center)));
        m_resolve.setUniform3fv(names.center, center);
        m_resolve.setUniform3fv(names.axisU, axisU);
        m_resolve.setUniform3fv(names.axisV, axisV);
        m_resolve.setUniform1f(names.lod, static_cast<float>(m_planar.picture(i).mipCount() - 1));
        m_planar.picture(i).bindSlot(names.slot);
    }
}

void GLReflectionPass::trace(GLFrameContext& ctx, int step) {
    const RenderView& view = ctx.view;

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
    m_trace.setUniform1i("u_traceStep",    step);
    ctx.screenTri.emit();
}

} // namespace Vkm::Engine
