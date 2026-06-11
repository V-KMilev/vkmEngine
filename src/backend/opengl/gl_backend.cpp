#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <string>
#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "logger.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "data/gl_probe.h"
#include "data/gl_probe_baker.h"
#include "convention/gl_bindings.h"
#include "gl_uniform_buffer.h"
#include "pass/gl_shadow_pass.h"
#include "pass/gl_depth_prepass.h"
#include "pass/gl_gtao_pass.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_skybox_pass.h"
#include "pass/gl_bloom_pass.h"
#include "pass/gl_ssr_pass.h"
#include "pass/gl_motion_blur_pass.h"
#include "pass/gl_composite_pass.h"
#include "data/gl_ibl_baker.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
// Matches ProbeBlock in shaders/forward/pbr (std140). One entry per probe.
struct GpuProbe {
    glm::vec4 center;    ///< xyz world centre, w pad
    glm::vec4 extents;   ///< xyz half-extents, w pad
    glm::vec4 params;    ///< x falloff, y intensity, z layer index, w pad
};
struct ProbeBlock {
    GpuProbe probes[GLBindings::ProbeTextureSlots::MAX_PROBES];
};
}

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL) {}

GLBackend::~GLBackend() = default;

bool GLBackend::init(WindowManager& window) {
    (void)window;  // GLEW + the GL context are created during Window creation;
                   // we draw into the already-current context. Presentation
                   // (buffer swap) stays in the engine loop, so we never swap.

    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    m_context.setDefaultState();
    m_context.setDepthTest(true);
    m_context.setFaceCulling(false);

    // The scene target carries a G-buffer (view normal + roughness + metalness),
    // written by the depth prepass and read by SSR. Enable before the first resize.
    m_sceneHDR.enableGBuffer();

    m_shadowAtlas.init();

    // Bake the image-based lighting product set once from the default
    // environment HDR (split-sum: env cube -> irradiance + prefilter + BRDF
    // LUT). The forward pass samples it for ambient and the skybox pass draws
    // the environment. A load failure leaves IBL off (forward falls back to
    // flat ambient). The baker's bake-only programs/meshes are transient.
    {
        GLIBLBaker baker;
        baker.bake(m_context, m_ibl, "assets/envs/environment.hdr");
    }

    // Build the pass list. Passes compile their shaders, so this must run after
    // the context exists. Order: shadow depth maps; a depth prepass (early-Z,
    // and it clears the HDR target for the frame); GTAO turns the prepass depth +
    // G-buffer into an occlusion factor the forward pass reads; the skybox fills
    // the background BEFORE geometry, so sorted transparents blend over it
    // instead of being overwritten; the lit forward draw (opaque then
    // transparent); screen-space reflections; camera motion blur over the
    // resolved scene; bloom; then composite (tonemap + FXAA) to screen.
    m_passes.push_back(std::make_unique<GLShadowPass>());
    m_passes.push_back(std::make_unique<GLDepthPrePass>());
    m_passes.push_back(std::make_unique<GLGTAOPass>());
    m_passes.push_back(std::make_unique<GLSkyboxPass>());
    m_passes.push_back(std::make_unique<GLForwardPass>());
    m_passes.push_back(std::make_unique<GLSSRPass>());
    m_passes.push_back(std::make_unique<GLMotionBlurPass>());
    m_passes.push_back(std::make_unique<GLBloomPass>());
    m_passes.push_back(std::make_unique<GLCompositePass>());

    // The probe baker compiles its own shaders, so build it here (context live).
    m_probeBaker = std::make_unique<GLProbeBaker>();

    // Shared probe cube-map arrays: one cube per layer for irradiance + prefilter.
    m_probeArray = std::make_unique<GLProbeArray>();
    m_probeArray->createTargets(static_cast<int>(GLBindings::ProbeTextureSlots::MAX_PROBES));

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* device  = glGetString(GL_RENDERER);
    m_info.api    = version ? "OpenGL " + std::string(reinterpret_cast<const char*>(version)) : "OpenGL";
    m_info.device = device  ? std::string(reinterpret_cast<const char*>(device)) : "";
    LOG_INFO("%s on %s", m_info.api.c_str(), m_info.device.c_str());

    return true;
}

void GLBackend::resize(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    m_context.setViewport(
        static_cast<int32_t>(x),
        static_cast<int32_t>(y),
        static_cast<int32_t>(width),
        static_cast<int32_t>(height)
    );
}

void GLBackend::render(const RenderView& view, const ResourceManager& resources) {
    m_view.sync(view, resources);
    m_sceneHDR.resize(view.viewportWidth, view.viewportHeight);
    m_sceneColor.resize(view.viewportWidth, view.viewportHeight);
    m_ao.resize(view.viewportWidth, view.viewportHeight);
    m_bloom.resize(view.viewportWidth, view.viewportHeight);

    // Plan the frame's shadows first: it assigns each light an atlas slot, which
    // the lights UBO then carries (spot.w), and uploads the ShadowBlock UBO.
    m_shadowData.build(view);

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    m_camera.update(view.camera);
    m_lights.update(view.lights, m_shadowData);
    m_shadowData.uploadAndBind();

    // Each pass binds and clears its own target: the shadow pass fills the depth
    // atlas, the forward pass renders the lit scene into m_sceneHDR sampling it,
    // and the composite pass tonemaps that to the backbuffer.
    GLFrameContext ctx{view, m_view, m_context, m_sceneHDR, m_sceneColor, m_shadowAtlas, m_shadowData, m_ibl, m_bloom, m_ao};

    // Reflection probes: pack the baked probes (nearest MAX_PROBES) into the
    // ProbeBlock UBO and bind the two cube-map arrays; the forward pass blends
    // them per fragment over the global IBL.
    ctx.probeCount = 0;
    if (m_probeArray) {
        struct Active { uint32_t index; float dist; };
        std::vector<Active> active;
        const int capacity = m_probeArray->capacity();
        for (size_t i = 0; i < view.probes.size() && static_cast<int>(i) < capacity; ++i) {
            if (i >= m_probeBaked.size() || !m_probeBaked[i]) continue;
            active.push_back({ static_cast<uint32_t>(i),
                glm::distance(view.camera.position, view.probes[i].position) });
        }
        const size_t maxP = GLBindings::ProbeTextureSlots::MAX_PROBES;
        if (active.size() > maxP) {
            std::partial_sort(active.begin(), active.begin() + maxP, active.end(),
                [](const Active& a, const Active& b) { return a.dist < b.dist; });
            active.resize(maxP);
        }

        ProbeBlock block{};
        for (size_t p = 0; p < active.size(); ++p) {
            const ProbeData& pd = view.probes[active[p].index];
            block.probes[p].center  = glm::vec4(pd.position, 0.0f);
            block.probes[p].extents = glm::vec4(pd.halfExtents, 0.0f);
            block.probes[p].params  = glm::vec4(pd.falloff, pd.intensity, static_cast<float>(active[p].index), 0.0f);
        }
        if (!m_probeUBO) m_probeUBO = std::make_unique<Core::UniformBuffer>(&block, sizeof(block), GL_DYNAMIC_DRAW);
        else             m_probeUBO->update(&block, sizeof(block));
        m_probeUBO->bindBase(GLBindings::UBOBindingPoints::Probes);

        m_probeArray->bindIrradiance(GLBindings::ProbeTextureSlots::Irradiance);
        m_probeArray->bindPrefilter(GLBindings::ProbeTextureSlots::Prefilter);
        ctx.probeCount = static_cast<int>(active.size());
    }

    for (const auto& pass : m_passes) {
        pass->execute(ctx);
    }

    // Frame end: bake any probe layer not yet baked. The baker rebinds the
    // camera / light UBOs, harmless here - the next frame re-uploads its own,
    // and the layer is ready for that frame.
    if (m_probeBaker && m_probeArray) {
        const int capacity = m_probeArray->capacity();
        const int n = std::min(static_cast<int>(view.probes.size()), capacity);
        if (static_cast<int>(m_probeBaked.size()) < n) m_probeBaked.resize(n, false);
        for (int i = 0; i < n; ++i) {
            if (m_probeBaked[i]) continue;
            m_probeBaker->bake(m_context, *m_probeArray, i, view.probes[i].position, view, m_view, m_ibl);
            m_probeBaked[i] = true;
        }
    }
}

} // namespace Engine
