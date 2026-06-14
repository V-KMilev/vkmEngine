#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <cstdint>
#include <string>

#include <GL/glew.h>

#include "logger.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "pass/gl_shadow_pass.h"
#include "pass/gl_depth_prepass.h"
#include "pass/gl_gtao_pass.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_skybox_pass.h"
#include "pass/gl_bloom_pass.h"
#include "pass/gl_ssr_pass.h"
#include "pass/gl_motion_blur_pass.h"
#include "pass/gl_grid_pass.h"
#include "pass/gl_composite_pass.h"
#include "data/gl_ibl_baker.h"
#include "data/gl_material.h"
#include "system/render/render_view.h"

#include "debug/profiler_gl.h"

namespace Engine {

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL) {}

GLBackend::~GLBackend() = default;

bool GLBackend::init(WindowManager& window) {
    (void)window;  // GLEW + the GL context are created during Window creation;
                   // we draw into the already-current context. Presentation
                   // (buffer swap) stays in the engine loop, so we never swap.

    // Register this GL context with the GPU profiler now that it is live; every
    // PROFILE_GPU_SCOPE downstream times against it. A backend hot-swap re-runs
    // init() and registers a second context, which is harmless.
    PROFILE_GPU_CONTEXT();

    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    m_context.setDefaultState();
    m_context.setDepthTest(true);
    m_context.setFaceCulling(false);

    // The scene target carries a G-buffer (view normal + roughness + metalness),
    // written by the depth prepass and read by SSR. Enable before the first resize.
    m_sceneHDR.enableGBuffer();

    m_shadowAtlas.init();


    // Build the pass list. Passes compile their shaders, so this must run after
    // the context exists. Order: shadow depth maps; a depth prepass (early-Z,
    // and it clears the HDR target for the frame); GTAO turns the prepass depth +
    // G-buffer into an occlusion factor the forward pass reads; the skybox fills
    // the background BEFORE geometry, so sorted transparents blend over it
    // instead of being overwritten; the lit forward draw (opaque then
    // transparent); screen-space reflections; camera motion blur over the
    // resolved scene; bloom; debug overlays (grid) over
    // the resolved HDR; then composite (tonemap + FXAA) to screen.
    m_passes.push_back({"Shadow",       std::make_unique<GLShadowPass>()});
    m_passes.push_back({"DepthPrepass", std::make_unique<GLDepthPrePass>()});
    m_passes.push_back({"GTAO",         std::make_unique<GLGTAOPass>()});
    m_passes.push_back({"Skybox",       std::make_unique<GLSkyboxPass>()});
    m_passes.push_back({"Forward",      std::make_unique<GLForwardPass>()});
    m_passes.push_back({"SSR",          std::make_unique<GLSSRPass>()});
    m_passes.push_back({"MotionBlur",   std::make_unique<GLMotionBlurPass>()});
    m_passes.push_back({"Bloom",        std::make_unique<GLBloomPass>()});
    m_passes.push_back({"Grid",         std::make_unique<GLGridPass>()});
    m_passes.push_back({"Composite",    std::make_unique<GLCompositePass>()});

    // Reflection probes: the baker + shared cube-map arrays. Compiles shaders, so
    // build it here (context live).
    m_probes.init();

    // Editor previews: compiles the same forward/composite shaders, so it also
    // needs the live context.
    m_preview.init();

    const std::string version = m_context.versionString();
    m_info.api    = version.empty() ? "OpenGL" : "OpenGL " + version;
    m_info.device = m_context.rendererString();
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
    PROFILE_SCOPE("GLBackend::render");
    PROFILE_GPU_SCOPE("GPU.Frame");

    m_view.sync(view, resources);
    m_sceneHDR.resize(view.viewportWidth, view.viewportHeight);
    m_sceneColor.resize(view.viewportWidth, view.viewportHeight);
    m_ao.resize(view.viewportWidth, view.viewportHeight);
    m_bloom.resize(view.viewportWidth, view.viewportHeight);

    // Bake the IBL on the first frame (m_bakedEnvPath empty) and re-bake whenever
    // the environment HDR changes (an editor swap or a scene load). The skybox
    // samples the baked product, so the background follows too.
    if (view.environment.hdrPath != m_bakedEnvPath) {
        bakeEnvironment(view.environment.hdrPath);
    }

    // Rebuild the shadow atlas if the editor changed its resolution (a no-op when
    // unchanged). Done before the shadow plan so both agree on the tile size.
    m_shadowAtlas.init(view.settings.shadowResolution);

    // Plan the frame's shadows first: it assigns each light an atlas slot, which
    // the lights UBO then carries (spot.w), and uploads the ShadowBlock UBO.
    m_shadowData.build(view);

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    m_camera.update(view.camera);
    m_lights.update(view.lights, m_shadowData);
    m_shadowData.uploadAndBind();

    // Bucket the drawables once; the prepass + forward both read the result.
    partitionDrawables(view);

    // Each pass binds and clears its own target: the shadow pass fills the depth
    // atlas, the forward pass renders the lit scene into m_sceneHDR sampling it,
    // and the composite pass tonemaps that to the backbuffer.
    GLFrameContext ctx{view, m_view, m_context, m_sceneHDR, m_sceneColor, m_shadowAtlas, m_shadowData, m_ibl, m_bloom, m_ao, m_opaque, m_transparent};

    // Reflection probes: pack the baked probes (nearest MAX_PROBES) into the
    // ProbeBlock UBO and bind the two cube-map arrays; the forward pass blends
    // them per fragment over the global IBL.
    ctx.probeCount = m_probes.bind(view);

    for (const auto& entry : m_passes) {
        PROFILE_SCOPE_NAMED(entry.name);
        PROFILE_GPU_SCOPE_NAMED(entry.name);
        entry.pass->execute(ctx);
    }

    // Per-frame counters for the profiler's plot view: how much the frame asked
    // the GPU to draw. Watch these alongside the pass zones to correlate spikes.
    PROFILE_PLOT("Render/Drawables",   static_cast<int64_t>(view.drawables.size()));
    PROFILE_PLOT("Render/Opaque",      static_cast<int64_t>(m_opaque.size()));
    PROFILE_PLOT("Render/Transparent", static_cast<int64_t>(m_transparent.size()));
    PROFILE_PLOT("Render/Lights",      static_cast<int64_t>(view.lights.size()));
    PROFILE_PLOT("Render/Probes",      static_cast<int64_t>(ctx.probeCount));

    // Frame end: re-bake probes that are new, moved, or version-bumped. The baker
    // rebinds the camera / light UBOs, harmless here - the next frame re-uploads
    // its own.
    m_probes.update(m_context, view, m_view, m_ibl);

    PROFILE_GPU_COLLECT();
}

void GLBackend::bakeEnvironment(const std::string& path) {
    // The baker's programs/meshes are transient - construct, bake, let it die.
    // A load failure leaves m_ibl not-ready (forward falls back to flat ambient).
    GLIBLBaker baker;
    baker.bake(m_context, m_ibl, path);
    m_bakedEnvPath = path;
}

void GLBackend::partitionDrawables(const RenderView& view) {
    m_opaque.clear();
    m_transparent.clear();
    m_opaque.reserve(view.drawables.size());

    // Transparent draws blended back-to-front in the forward pass; everything
    // else (Opaque / AlphaMask / Unlit) primes depth and draws first. An
    // unresolved material reads as opaque, matching the forward pass fallback.
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = m_view.getMaterial(d.material);
        if (material && material->getType() == MaterialType::Transparent) {
            m_transparent.push_back(&d);
        } else {
            m_opaque.push_back(&d);
        }
    }
}

uint32_t GLBackend::renderPreview(const PreviewRequest& request,
                                  const ResourceManager& resources) {
    // Runs from the editor after the scene render; like the probe baker it
    // re-binds the camera / lights UBOs, which the next frame re-uploads.
    return m_preview.render(m_context, m_view, m_ibl, request, resources);
}

uint32_t GLBackend::previewTexture(uint64_t key) const {
    return m_preview.texture(key);
}

void GLBackend::releasePreview(uint64_t key) {
    m_preview.release(key);
}

void GLBackend::releaseAllPreviews() {
    m_preview.releaseAll();
}

} // namespace Engine
