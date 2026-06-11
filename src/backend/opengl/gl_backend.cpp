#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

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
#include "pass/gl_composite_pass.h"
#include "data/gl_ibl_baker.h"
#include "system/render/render_view.h"

namespace Engine {

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
    // resolved scene; bloom; debug overlays (grid) over
    // the resolved HDR; then composite (tonemap + FXAA) to screen.
    m_passes.push_back(std::make_unique<GLShadowPass>());
    m_passes.push_back(std::make_unique<GLDepthPrePass>());
    m_passes.push_back(std::make_unique<GLGTAOPass>());
    m_passes.push_back(std::make_unique<GLSkyboxPass>());
    m_passes.push_back(std::make_unique<GLForwardPass>());
    m_passes.push_back(std::make_unique<GLSSRPass>());
    m_passes.push_back(std::make_unique<GLMotionBlurPass>());
    m_passes.push_back(std::make_unique<GLBloomPass>());
    m_passes.push_back(std::make_unique<GLCompositePass>());

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
    ctx.probeCount = m_probes.bind(view);

    for (const auto& pass : m_passes) {
        pass->execute(ctx);
    }

    // Frame end: re-bake probes that are new, moved, or version-bumped. The baker
    // rebinds the camera / light UBOs, harmless here - the next frame re-uploads
    // its own.
    m_probes.update(m_context, view, m_view, m_ibl);
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
