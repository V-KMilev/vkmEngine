#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_backend.h"

#include <cstdint>
#include <string>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "logger.h"

#include "gl_frame_context.h"
#include "gl_pass.h"
#include "gl_shader.h"
#include "texture/gl_texture.h"
#include "pass/gl_shadow_pass.h"
#include "pass/gl_depth_prepass.h"
#include "pass/gl_resolve_pass.h"
#include "pass/gl_cluster_pass.h"
#include "pass/gl_contact_shadow_pass.h"
#include "pass/gl_fog_pass.h"
#include "pass/gl_fog_apply_pass.h"
#include "pass/gl_dof_pass.h"
#include "pass/gl_decal_pass.h"
#include "pass/gl_particle_pass.h"
#include "pass/gl_gtao_pass.h"
#include "pass/gl_forward_pass.h"
#include "pass/gl_skybox_pass.h"
#include "pass/gl_bloom_pass.h"
#include "pass/gl_grid_pass.h"
#include "pass/gl_composite_pass.h"
#include "pass/gl_ui_pass.h"
#include "data/gl_ibl_baker.h"
#include "data/gl_material.h"
#include "system/render/render_view.h"
#include "resource/resource_manager.h"
#include "platform/window/window_manager.h"

#include "debug/profiler_gl.h"

namespace Engine {

namespace {
// The procedural sky follows the scene's primary directional light so the sky
// and the key light agree. Returns the direction TO the sun and sets hasSun; a
// sensible high sun (hasSun = false) when the scene has no directional light.
glm::vec3 primarySunDir(const RenderView& view, bool& hasSun) {
    for (const LightData& light : view.lights) {
        if (light.type == LightType::Directional) {
            const glm::vec3 toSun = -light.direction;
            if (glm::dot(toSun, toSun) > 1e-6f) {
                hasSun = true;
                return glm::normalize(toSun);
            }
        }
    }
    hasSun = false;
    return glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
}
} // namespace

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

    // Transparent black, the GL default: background HDR / G-buffer pixels stay
    // zero (G-buffer alpha is metalness - a non-zero clear would give empty
    // pixels metal).
    m_context.setClearColor(glm::vec4(0.0f));
    m_context.setDefaultState();
    // Start with face culling off (overriding setDefaultState's cull-on): every
    // geometry pass sets its own cull state and the fullscreen passes disable it,
    // so this just makes the frame-start default explicit.
    m_context.setFaceCulling(false);

    // The scene target carries a G-buffer (view normal + roughness + metalness),
    // written by the depth prepass and read by GTAO + the decal pass. Enable before the first
    // resize, on both the resolved target and the multisample one the geometry
    // passes render into when MSAA is on.
    m_sceneHDR.enableGBuffer();
    m_sceneMS.enableGBuffer();

    // The post chain's ping-pong scratches carry colour only - the post passes
    // depth-test nothing and sample the geometry target's depth as a texture.
    m_postA.setColorOnly();
    m_postB.setColorOnly();

    // Shaders omit their own #version; inject it from the requested GL context
    // version (single source of truth) before the passes compile their programs.
    Core::setGraphicsShaderVersion(OPENGL_GLSL_VERSION);

    // Build the pass list. Passes compile their shaders, so this must run after
    // the context exists. One line per pass, in execution order:
    //   Shadow        - depth maps into the shadow atlas (2D tiles + point cubes).
    //   DepthPrepass  - clears the scene target; primes opaque depth + G-buffer.
    //   ResolveDepth  - MSAA only: resolves depth (+ G-buffer when read) for the
    //                   screen-space passes.
    //   GTAO          - occlusion factor + bent normal from resolved depth.
    //   ContactShadow - screen-space sun visibility mask (skips without a sun).
    //   Skybox        - fills the background BEFORE geometry, so sorted
    //                   transparents blend over it instead of being overwritten.
    //   ClusterCull   - compute: Forward+ per-cluster light lists.
    //   FogCompute    - compute: froxel inject + integrate (lazy-allocates).
    //   Forward       - the lit draw: opaque, alpha-mask, then transparents.
    //   Particles     - billboards into the scene target, depth-tested.
    //   ResolveColor  - MSAA only: resolves colour (+ depth if alpha-mask drew).
    //   Decals        - projected boxes, blended into the post colour chain.
    //   FogApply      - composites the integrated fog   (chain: src -> dst).
    //   DoF           - circle-of-confusion disk blur   (chain: src -> dst).
    //   Bloom         - mip pyramid off the chain; composite blends it back.
    //   Grid          - editor overlay into the chain (shader-side depth test).
    //   Composite     - tonemap + debug views, to the backbuffer viewport.
    //   UI            - the in-game UI overlay, flat on the backbuffer.
    m_passes.push_back({"Shadow",         std::make_unique<GLShadowPass>()});
    m_passes.push_back({"DepthPrepass",   std::make_unique<GLDepthPrepass>()});
    m_passes.push_back({"ResolveDepth",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Geometry)});
    m_passes.push_back({"GTAO",           std::make_unique<GLGTAOPass>()});
    m_passes.push_back({"ContactShadow",  std::make_unique<GLContactShadowPass>()});
    m_passes.push_back({"Skybox",         std::make_unique<GLSkyboxPass>()});
    m_passes.push_back({"ClusterCull",    std::make_unique<GLClusterPass>()});
    m_passes.push_back({"FogCompute",     std::make_unique<GLFogPass>()});
    m_passes.push_back({"Forward",        std::make_unique<GLForwardPass>()});
    m_passes.push_back({"Particles",      std::make_unique<GLParticlePass>()});
    m_passes.push_back({"ResolveColor",   std::make_unique<GLResolvePass>(GLResolvePass::Scope::Color)});
    m_passes.push_back({"Decals",         std::make_unique<GLDecalPass>()});
    m_passes.push_back({"FogApply",       std::make_unique<GLFogApplyPass>()});
    m_passes.push_back({"DoF",            std::make_unique<GLDoFPass>()});
    m_passes.push_back({"Bloom",          std::make_unique<GLBloomPass>()});
    m_passes.push_back({"Grid",           std::make_unique<GLGridPass>()});
    m_passes.push_back({"Composite",      std::make_unique<GLCompositePass>()});
    m_passes.push_back({"UI",             std::make_unique<GLUIPass>()});

    // GTAO writes an occlusion factor plus a packed bent normal, so its target
    // needs more than one channel (the contact-shadow mask stays R16F).
    m_ao.setFormat(GL_RGBA16F, GL_RGBA);

    // Forward+ cluster light grid: allocate its SSBO now the context is live.
    m_clusterGrid.init();

    // Froxel fog volumes allocate lazily - the fog pass inits them on the first
    // fog-enabled frame, so scenes that never enable fog never pay the ~15 MB.

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

    // Before anything reads a cache: if the scene was replaced, none of them
    // can be trusted, and nothing downstream can tell on its own.
    onAssetGraphSwapped(resources);

    {
        PROFILE_SCOPE("Render/SyncAssets");
        m_view.sync(view, resources);
    }
    m_sceneHDR.resize(view.viewportWidth, view.viewportHeight);
    m_postA.resize(view.viewportWidth, view.viewportHeight);
    m_postB.resize(view.viewportWidth, view.viewportHeight);
    m_bloom.resize(view.viewportWidth, view.viewportHeight);

    // Mask targets only exist while something reads them (the composite debug
    // views sample the AO target unconditionally, so any non-default view keeps
    // it alive too). Once allocated they stay - toggles flip too often to thrash.
    if (view.settings.gtao || view.settings.renderMode != RenderMode::Default)
        m_ao.resize(view.viewportWidth, view.viewportHeight);
    if (view.settings.contactShadows)
        m_contactShadow.resize(view.viewportWidth, view.viewportHeight);

    // MSAA: when enabled, the geometry passes render into the multisample target
    // and GLResolvePass resolves it into m_sceneHDR; the whole post chain stays
    // single-sample. When off, they render straight into m_sceneHDR (resolve
    // passes no-op) and the MS storage is released - at 4x it is the largest
    // allocation in the frame.
    const uint32_t samples = view.settings.msaaSamples;
    if (samples > 1) {
        m_sceneMS.setSamples(samples, m_context);
        m_sceneMS.resize(view.viewportWidth, view.viewportHeight);
    } else {
        m_sceneMS.release();
    }
    GLTarget& sceneRender = (samples > 1) ? m_sceneMS : m_sceneHDR;

    // Direction to the sun (primary directional light); drives the procedural
    // sky bake and the skybox sun disc, so compute it once here.
    bool hasSun = false;
    const glm::vec3 sunDir = primarySunDir(view, hasSun);

    // Bake the IBL and re-bake on change. The procedural atmosphere follows the
    // sun (re-baking when it or a sky param moves); otherwise the equirect HDR is
    // baked when hdrPath changes. The skybox samples the baked product, so the
    // background follows too.
    if (view.environment.proceduralSky) {
        if (skyNeedsRebake(view.environment, sunDir)) {
            bakeProceduralSky(view.environment, sunDir);
        }
    } else if (view.environment.hdrPath != m_bakedEnvPath) {
        bakeEnvironment(view.environment.hdrPath);
    }

    // Rebuild the shadow atlas if the editor changed its resolution (a no-op when
    // unchanged). Done before the shadow plan so both agree on the tile size.
    {
        PROFILE_SCOPE("Render/ShadowAtlasInit");
        m_shadowAtlas.init(view.settings.shadowResolution);
    }

    // Plan the frame's shadows first: it assigns each light an atlas slot, which
    // the lights UBO then carries (spot.w), and uploads the ShadowBlock UBO.
    {
        PROFILE_SCOPE("Shadow/Plan");
        m_shadowData.build(view);
    }

    // Per-frame UBOs: uploaded and bound once here, visible to every pass.
    {
        PROFILE_SCOPE("Render/FrameUBOs");
        m_camera.update(view.camera);
        m_lights.update(view.lights, m_shadowData);
        m_shadowData.uploadAndBind();
    }

    // Bucket the drawables once; the prepass + forward both read the result.
    {
        PROFILE_SCOPE("Render/Partition");
        partitionDrawables(view);
    }

    // ...and batch the opaque bucket once too. Both passes draw this identical
    // list, so batching it per-pass sorted every drawable and re-uploaded both
    // instance buffers twice a frame to produce the same runs.
    {
        PROFILE_SCOPE("Render/OpaqueBatch");
        m_opaqueBatcher.buildGrouped(m_opaque, m_view);
    }

    // Each pass binds and clears its own target: the shadow pass fills the depth
    // atlas, the forward pass renders the lit scene into m_sceneHDR sampling it,
    // and the composite pass tonemaps that to the backbuffer.
    GLFrameContext ctx{
        view,
        m_view,
        m_context,
        m_screenTri,
        m_sceneHDR,
        sceneRender,
        m_shadowAtlas,
        m_shadowData,
        m_ibl,
        m_bloom,
        m_ao,
        m_contactShadow,
        m_clusterGrid,
        m_fog,
        m_irradiance,
        m_opaque,
        m_alphaMask,
        m_transparent,
        m_opaqueBatcher};

    ctx.sunDir = sunDir;
    ctx.hasSun = hasSun;

    // Post colour chain: the scene starts on the geometry target; the first
    // post pass moves it into a scratch and the chain ping-pongs from there.
    ctx.scratchA = &m_postA;
    ctx.scratchB = &m_postB;
    ctx.colorSrc = &m_sceneHDR;
    ctx.colorDst = &m_postA;

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
    PROFILE_PLOT("Render/AlphaMask",   static_cast<int64_t>(m_alphaMask.size()));
    PROFILE_PLOT("Render/Transparent", static_cast<int64_t>(m_transparent.size()));
    PROFILE_PLOT("Render/Lights",      static_cast<int64_t>(view.lights.size()));
    PROFILE_PLOT("Render/Probes",      static_cast<int64_t>(ctx.probeCount));

    // Frame end: re-bake the irradiance volume when its box, grid, or version
    // changed. Like the probe bake this rebinds the camera / light UBOs, which
    // the next frame re-uploads.
    if (!view.irradianceVolumes.empty()) {
        const IrradianceVolumeData& iv = view.irradianceVolumes[0];
        const BakedIrradiance& b = m_bakedIrradiance;
        const bool dirty = !b.valid
            || b.center != iv.center || b.halfExtents != iv.halfExtents
            || b.resolutionX != iv.resolutionX || b.resolutionY != iv.resolutionY
            || b.resolutionZ != iv.resolutionZ || b.bakeVersion != iv.bakeVersion;
        if (dirty) {
            m_irradiance.resize(iv.resolutionX, iv.resolutionY, iv.resolutionZ);
            m_irradianceBaker.bake(m_context, m_irradiance, iv, view, m_view, m_ibl);
            m_bakedIrradiance = { true, iv.center, iv.halfExtents,
                                  iv.resolutionX, iv.resolutionY, iv.resolutionZ, iv.bakeVersion };
        }
    }

    // Frame end: re-bake probes that are new, moved, or version-bumped. The baker
    // rebinds the camera / light UBOs, harmless here - the next frame re-uploads
    // its own.
    m_probes.update(m_context, view, m_view, m_ibl);

    PROFILE_GPU_COLLECT();
}

void GLBackend::bakeEnvironment(const std::string& path) {
    // A load failure leaves m_ibl not-ready (forward falls back to flat ambient).
    m_iblBaker.bake(m_context, m_ibl, path);
    m_bakedEnvPath    = path;
    m_bakedSky.active = false;  // an HDR is baked now, not the procedural sky
}

bool GLBackend::skyNeedsRebake(const Environment& env, const glm::vec3& sunDir) const {
    const BakedSky& b = m_bakedSky;
    if (!b.active) return true;
    if (env.skySunIntensity != b.sunIntensity) return true;
    if (env.skyRayleigh     != b.rayleigh)     return true;
    if (env.skyMie          != b.mie)          return true;
    if (env.skyMieG         != b.mieG)         return true;
    return glm::dot(sunDir, b.sunDir) < 0.99995f;  // sun moved enough to matter
}

void GLBackend::bakeProceduralSky(const Environment& env, const glm::vec3& sunDir) {
    SkyParams sky;
    sky.sunDir       = sunDir;
    sky.sunIntensity = env.skySunIntensity;
    sky.rayleigh     = env.skyRayleigh;
    sky.mie          = env.skyMie;
    sky.mieG         = env.skyMieG;

    m_iblBaker.bakeProcedural(m_context, m_ibl, sky);

    m_bakedSky = {true, sunDir, env.skySunIntensity, env.skyRayleigh, env.skyMie, env.skyMieG};
    m_bakedEnvPath.clear();  // force an HDR re-bake if the user switches back
}

void GLBackend::onAssetGraphSwapped(const ResourceManager& resources) {
    const uint64_t epoch = resources.epoch();
    if (epoch == m_assetEpoch) return;
    m_assetEpoch = epoch;

    PROFILE_SCOPE("Render/GraphSwap");

    m_view.invalidate();      // asset mirrors: handles and versions restart
    m_probes.invalidate();    // cube captures: same probe pose, different scene
    m_bakedIrradiance = {};   // SH volume: same box and grid, different scene

    LOG_INFO("Asset graph swapped; scene-derived GPU caches dropped");
}

void GLBackend::partitionDrawables(const RenderView& view) {
    m_opaque.clear();
    m_alphaMask.clear();
    m_transparent.clear();
    m_opaque.reserve(view.drawables.size());

    // Opaque / Unlit prime depth in the prepass and draw first (early-Z).
    // AlphaMask skips the prepass and draws in the forward pass with depth
    // writes on + alpha-to-coverage, so its cutout edges anti-alias under MSAA.
    // Transparent draws blended back-to-front last. An unresolved material reads
    // as opaque, matching the forward pass fallback.
    for (const DrawableData& d : view.drawables) {
        const GLMaterial* material = m_view.getMaterial(d.material);
        const MaterialType type = material ? material->getType() : MaterialType::Opaque;
        if (type == MaterialType::Transparent)    m_transparent.push_back(&d);
        else if (type == MaterialType::AlphaMask) m_alphaMask.push_back(&d);
        else                                      m_opaque.push_back(&d);
    }
}

GpuTextureId GLBackend::renderPreview(const PreviewRequest& request,
                                  const ResourceManager& resources) {
    // Runs from the editor after the scene render; like the probe baker it
    // re-binds the camera / lights UBOs, which the next frame re-uploads.
    return m_preview.render(m_context, m_view, m_ibl, request, resources);
}

GpuTextureId GLBackend::previewTexture(uint64_t key) const {
    return m_preview.texture(key);
}

void GLBackend::releasePreview(uint64_t key) {
    m_preview.release(key);
}

GpuTextureId GLBackend::textureId(const TextureHandle& handle) const {
    const Core::Texture2D* tex = m_view.getTexture(handle);
    return tex ? tex->getID() : 0;
}

void GLBackend::releaseAllPreviews() {
    m_preview.releaseAll();
}

} // namespace Engine
