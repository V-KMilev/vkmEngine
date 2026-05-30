#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_forward_pass.h"

#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <GL/glew.h>

#include "logger.h"
#include "texture/gl_texture.h"

#include "core/gl_backend.h"
#include "core/gl_instance_batcher.h"
#include "core/gl_scene_target.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_ibl.h"
#include "resource/gl_material.h"
#include "resource/gl_mesh.h"
#include "resource/gl_oit.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_shadow_map.h"
#include "resource/gl_sss_lut.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {

/**
 * @brief RAII guard: restore GL_FILL polygon mode on destruction.
 *
 * Used to wrap the wireframe set/restore around the body of execute() so every
 * return path (including the transparent-phase "no transparent batches" early
 * exit) leaves polygon mode back at GL_FILL - otherwise post passes rasterize
 * as line segments and ImGui draws as outlines.
 */
struct PolygonModeGuard {
    Core::Context* ctx;
    ~PolygonModeGuard() { if (ctx) ctx->setPolygonMode(GL_FRONT_AND_BACK, GL_FILL); }
};

} // namespace

namespace {
const char* phaseSuffix(GLForwardPass::Phase p) {
    switch (p) {
        case GLForwardPass::Phase::Opaque:      return " (Opaque)";
        case GLForwardPass::Phase::Transparent: return " (Transparent)";
        case GLForwardPass::Phase::All:         return "";
    }
    return "";
}

/**
 * @brief The IBL chosen for this frame. A reflection probe overrides the global
 * IBL when the camera is inside its radius (nearest such probe by centre
 * distance wins). The forward pass binds one set per frame; per-batch selection
 * is a future refinement.
 */
struct ActiveIBL {
    const GLIBL* ibl        = nullptr;
    float        intensity  = 1.0f;
    int          probeIndex = -1;  ///< -1 = global (no probe)
};

ActiveIBL selectActiveIBL(const RenderView& view, GLView& glView, const GLIBL& globalIBL) {
    ActiveIBL sel{ &globalIBL, view.environment.ibl.intensity, -1 };
    const auto& probes = view.probes;
    const auto& pool   = glView.getProbeIBLs();
    const glm::vec3 cam = view.camera.position;
    float bestDist = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < probes.size() && i < pool.size(); ++i) {
        const auto& p = probes[i];
        if (!pool[i] || !pool[i]->isReady()) continue;
        const float dist = glm::length(cam - p.position);
        if (dist > p.radius) continue;
        if (dist < bestDist) {
            bestDist       = dist;
            sel.ibl        = pool[i].get();
            sel.intensity  = p.intensity;
            sel.probeIndex = static_cast<int>(i);
        }
    }
    return sel;
}

/**
 * @brief Coarse per-frame shader-variant key pieces: a light-count bucket and a
 * mask of which shadow-casting light kinds are present. Coarse on purpose - a
 * handful of compile-time paths, not a unique program per scene.
 */
struct LightShadowKey {
    uint8_t lightCountBucket = 0;
    uint8_t shadowKindMask   = 0;
};

LightShadowKey computeLightShadowKey(const RenderView& view) {
    LightShadowKey k;
    const size_t n = view.lights.size();
    if (n == 0)      k.lightCountBucket = 0;
    else if (n == 1) k.lightCountBucket = 1;
    else if (n <= 4) k.lightCountBucket = 2;
    else             k.lightCountBucket = 3;
    for (const auto& l : view.lights) {
        if (l.shadowSlot < 0) continue;
        switch (l.type) {
            case LightType::Directional: k.shadowKindMask |= 0x1u; break;
            case LightType::Point:       k.shadowKindMask |= 0x2u; break;
            case LightType::Spot:        k.shadowKindMask |= 0x4u; break;
            default: break;  // Rect / Disk don't cast shadows yet.
        }
    }
    return k;
}

/**
 * @brief WireframeOverShaded overlay: re-draw every opaque batch as line
 * geometry into the HDR target's overlay attachment (composite blends it over
 * the tonemapped scene with no display transform). Polygon offset pulls the
 * wires in front of the fill. Runs in the Transparent phase only (it runs last).
 */
void drawWireframeOverlay(GLBackend& gl, GLView& glView, const RenderView& view,
                          const ResourceManager& resources, GLSceneTarget& hdrT,
                          ShaderHandle unlitHandle, ShaderHandle opaqueHandle,
                          const std::function<void(GLShader*)>& applyFrameUniforms) {
    ShaderHandle unlit = unlitHandle ? unlitHandle : opaqueHandle;
    GLShader* lineShader = glView.resolveShader(unlit, resources);
    if (!lineShader) return;

    auto&       glContext = gl.getContext();
    auto&       batcher   = glView.getInstanceBatcher();
    const auto& batches   = batcher.getBatches();

    hdrT.bindForOverlay();

    glContext.setPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
    glContext.setDepthTest(true);
    glContext.setDepthWrite(false);
    glContext.setBlending(false);
    glContext.setFaceCulling(false);

    lineShader->bind();
    applyFrameUniforms(lineShader);
    if (lineShader->hasUniform("u_lineOverlay"))
        lineShader->setUniform1i("u_lineOverlay", 1);

    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];
        if (batch.materialType == MaterialType::Transparent) continue;
        GLMesh* mesh = glView.getMutableMesh(batch.mesh);
        if (!mesh) continue;
        batcher.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
        mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.instanceCount, batch.firstInstance);
    }

    glContext.setPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glContext.setDepthWrite(true);
    if (lineShader->hasUniform("u_lineOverlay"))
        lineShader->setUniform1i("u_lineOverlay", 0);

    // Restore HDR-only routing so later passes see color attachment 0.
    hdrT.bindForRender();
}
}

GLForwardPass::GLForwardPass(ShaderHandle pbrShader, Phase phase)
    : GLRenderPass(std::string("GLForwardPass") + phaseSuffix(phase)), m_phase(phase) {
    m_shaders[static_cast<int>(MaterialType::Opaque)]      = pbrShader;
    m_shaders[static_cast<int>(MaterialType::Transparent)] = pbrShader;
    m_shaders[static_cast<int>(MaterialType::AlphaMask)]   = pbrShader;
    // Unlit stays empty until setShader() is called
}

GLForwardPass::~GLForwardPass() = default;

void GLForwardPass::setShader(MaterialType type, ShaderHandle shader) {
    m_shaders[static_cast<int>(type)] = shader;
}

void GLForwardPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for forward pass
}

void GLForwardPass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    auto& glContext = gl.getContext();

    // Graph-registered transient resources. Persistent backend state
    // (GLView / GLContext / default target) still comes through gl.
    auto& hdrT       = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& gbuffer    = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& ibl        = *rg.resource<GLIBL>(RGResource::IBL);
    auto& shadowAtlas = *rg.resource<GLShadowAtlas>(RGResource::ShadowAtlas);
    GLOIT* oit       = rg.resource<GLOIT>(RGResource::OITAccum);

    // Weighted-Blended OIT routes the transparent phase to a separate
    // single-sample MRT (accum, revealage). Off by default; falls back to
    // the sorted-with-refraction path when disabled or when no OIT target
    // is available.
    const bool oitActive = (m_phase == Phase::Transparent)
        && view.environment.transparency.useOIT
        && oit && oit->isReady();

    // Honor runtime MSAA-sample edits (env.msaa.samples) before anything
    // binds the target this frame. Idempotent - reallocates the scene target
    // only when the count actually changed. The opaque phase is the first
    // pass to touch the HDR target each frame, so doing it here is safe.
    if (m_phase != Phase::Transparent) {
        hdrT.ensureSamples(view.environment.msaa.samples);
    }

    // The scene renders into the offscreen linear-HDR target so light is
    // never clamped before tone mapping. The composite pass resolves this
    // and applies exposure + AgX to the backbuffer.
    hdrT.bindForRender();

    // The opaque (or legacy All) pass owns the clear. The transparent pass
    // must NOT clear - it refracts the opaque+sky scene drawn before it.
    // The overlay attachment also gets cleared here, once per frame, so
    // diagnostic passes (AABB / Grid) draw onto a fully transparent canvas
    // that the composite blends over the tonemapped scene afterwards.
    if (m_phase != Phase::Transparent) {
        glContext.setClearColor(view.environment.clearColor);
        glContext.clearColor();
        glContext.clear();
        hdrT.clearOverlay();
        // clearOverlay() leaves draw-buffer routed to the overlay attachment;
        // restore HDR-only routing for the rest of this pass.
        hdrT.bindForRender();
    }

    if (view.drawables.empty()) {
        return;
    }

    // Overdraw diagnostic: every shaded fragment additively accumulates a
    // small constant into the HDR target (see PBR fragment, u_debugMode == 11).
    // Set the blend / depth state up front for both phases so it overrides
    // the transparent-phase setup further down; restored at end of pass.
    // Depth-write is left off so the skybox between Opaque and Transparent
    // phases sees a stable depth buffer (the opaque clear is still in place).
    const bool overdraw = view.modeConfig.overdrawBlend;
    if (overdraw) {
        glContext.setBlending(true);
        glContext.setBlendFunc(GL_ONE, GL_ONE);
        glContext.setDepthTest(false);
        glContext.setDepthWrite(false);
    }

    // Wireframe is a geometry-pass concern only - applying polygon mode
    // globally would make fullscreen-triangle post passes rasterize as
    // line segments. The PolygonModeGuard in the anonymous namespace
    // above restores GL_FILL on every return path.
    const bool wireframe = view.modeConfig.wireframe;
    PolygonModeGuard wireframeGuard{ wireframe ? &glContext : nullptr };
    if (wireframe) glContext.setPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    auto& glView = gl.getView();

    auto& batcher = glView.getInstanceBatcher();
    const auto& batches = batcher.getBatches();

    // Bind both shadow atlases for the PBR shader to sample. The compare path
    // (slots 10/11) feeds the hardware-PCF samplers via sampler*Shadow types;
    // the raw-depth path (slots 21/22) binds the same textures with a
    // compare-off sampler object for PCSS's blocker search.
    shadowAtlas.bind2DForReading(GLConfig::TextureSlots::ShadowMap2D);
    shadowAtlas.bindCubeForReading(GLConfig::TextureSlots::ShadowMapCube);
    shadowAtlas.bind2DForReadingDepth(GLConfig::TextureSlots::ShadowMap2DDepth);
    shadowAtlas.bindCubeForReadingDepth(GLConfig::TextureSlots::ShadowMapCubeDepth);

    // Pick the frame's IBL (a probe overrides the global when the camera is
    // inside its radius) and bind the baked set below; the PBR shader blends the
    // primary against the global fallback per fragment.
    const ActiveIBL sel = selectActiveIBL(view, glView, ibl);
    const GLIBL* activeIBL            = sel.ibl;
    const float  activeProbeIntensity = sel.intensity;
    const int    activeProbeIndex     = sel.probeIndex;

    const bool iblReady = activeIBL && activeIBL->isReady();
    if (iblReady) {
        activeIBL->bindIrradiance(GLConfig::TextureSlots::IrradianceMap);
        activeIBL->bindPrefilter(GLConfig::TextureSlots::PrefilterMap);
        activeIBL->bindBrdf(GLConfig::TextureSlots::BrdfLUT);
        // Raw env cube too: the PBR shader blends a sharp env reflection in
        // at low roughness so polished metal reads as a true mirror, not the
        // prefilter's mip-0 GGX blur.
        activeIBL->bindEnvCube(GLConfig::TextureSlots::EnvCube);
    }

    // Global skybox bake always lands in the fallback slot set. The PBR
    // shader blends the primary (active probe or global) against this
    // fallback by a per-fragment weight derived from the probe's influence
    // sphere - so a probe-lit fragment smoothly fades back to the global
    // IBL at the edges of its radius instead of cutting off hard.
    // When no probe is selected, primary == fallback (both are the global)
    // and the blend collapses to a single sample - the same one the
    // pre-blend code path used. When the global isn't baked but a probe is,
    // fall back to the probe so the slots aren't sampled uninitialised - the
    // blend then samples the probe in both lobes which is the same as no
    // blend at all.
    const GLIBL* fallbackIBL = ibl.isReady() ? &ibl : activeIBL;
    if (fallbackIBL && fallbackIBL->isReady()) {
        fallbackIBL->bindIrradiance(GLConfig::TextureSlots::IrradianceMap2);
        fallbackIBL->bindPrefilter(GLConfig::TextureSlots::PrefilterMap2);
        fallbackIBL->bindEnvCube(GLConfig::TextureSlots::EnvCube2);
    }

    // Primary-probe metadata for the per-fragment blend. Only meaningful
    // when activeProbeIndex >= 0; the shader gates on u_probeValid so the
    // global-only path uses zero parallax correction and weight = 1.
    glm::vec3 probeCenter   = glm::vec3(0.0f);
    float     probeRadius   = 1.0f;
    float     probeFalloff  = 0.5f;
    if (activeProbeIndex >= 0 && static_cast<std::size_t>(activeProbeIndex) < view.probes.size()) {
        const auto& p = view.probes[activeProbeIndex];
        probeCenter  = p.position;
        probeRadius  = glm::max(p.radius, 1e-3f);
        probeFalloff = glm::max(p.falloffRange, 1e-3f);
    }
    // Screen-space AO from the prepass/GTAO (slot SSAO); enabled when both
    // the G-buffer is live and the environment toggle is on.
    const bool ssaoOn = gbuffer.isReady() && view.environment.ao.enabled;
    gbuffer.bindOcclusion(GLConfig::TextureSlots::SSAO);

    // Pre-integrated subsurface LUT. One-shot build the first frame the
    // pass runs; bound every frame so a HAS_SUBSURFACE shader variant can
    // sample it. Variants compiled without HAS_SUBSURFACE never reference
    // the sampler so the bind cost is just the one glActiveTexture call.
    if (!m_sssLUT) m_sssLUT = makeSSSLUT();
    if (m_sssLUT) m_sssLUT->bindSlot(GLConfig::TextureSlots::SssLUT);

    // Per-material shader variants mean we no longer have a single PBR
    // program to set frame-wide uniforms on up front. Capture them in a
    // local snapshot and re-apply whenever we bind a new variant - the
    // GL_INVALID_OPERATION-safe path is to just call setUniform after each
    // bind (locations are cached and missing names resolve to -1 no-op).
    struct PBRFrameUniforms {
        int   hasIBL;
        float iblIntensity;          // Primary IBL intensity (probe or global)
        float fallbackIBLIntensity;  // Always the global IBL intensity
        int   probeValid;            // 1 = primary slot is a probe (parallax + weight); 0 = global
        int   ssaoEnabled;
        float screenW;
        float screenH;
        int   hasSceneColor;  // flips 0 -> 1 at the opaque/transparent boundary
    };
    // When the global skybox bake isn't ready the fallback slots got the
    // probe's textures (see binding block above) - keep their intensity
    // matched so the blend collapses to a single weighted sample of the
    // probe instead of mixing the probe's HDR with the global intensity
    // configured for an unbaked environment.
    const float fallbackIntensity = ibl.isReady()
        ? view.environment.ibl.intensity
        : activeProbeIntensity;
    PBRFrameUniforms frame{
        iblReady ? 1 : 0,
        activeProbeIntensity,
        fallbackIntensity,
        activeProbeIndex >= 0 ? 1 : 0,
        ssaoOn  ? 1 : 0,
        static_cast<float>(view.viewportWidth),
        static_cast<float>(view.viewportHeight),
        0,
    };
    auto applyFrameUniforms = [&](GLShader* sh) {
        // u_screenSize and u_hasSceneColor get stripped from PBR variants
        // that don't compile HAS_TRANSMISSION; check hasUniform first so
        // we don't spam vkmGL's missing-uniform warning per shader switch.
        sh->setUniform1i("u_hasIBL", frame.hasIBL);
        sh->setUniform1f("u_iblIntensity", frame.iblIntensity);
        if (sh->hasUniform("u_iblIntensity2"))
            sh->setUniform1f("u_iblIntensity2", frame.fallbackIBLIntensity);
        if (sh->hasUniform("u_probeValid"))
            sh->setUniform1i("u_probeValid", frame.probeValid);
        if (sh->hasUniform("u_probeCenter"))
            sh->setUniform3fv("u_probeCenter", probeCenter);
        if (sh->hasUniform("u_probeRadius"))
            sh->setUniform1f("u_probeRadius", probeRadius);
        if (sh->hasUniform("u_probeFalloff"))
            sh->setUniform1f("u_probeFalloff", probeFalloff);
        sh->setUniform1i("u_ssaoEnabled", frame.ssaoEnabled);
        if (sh->hasUniform("u_shadowSoftness"))
            sh->setUniform1f("u_shadowSoftness", view.environment.shadow.softness);
        if (sh->hasUniform("u_screenSize"))
            sh->setUniform2f("u_screenSize", frame.screenW, frame.screenH);
        if (sh->hasUniform("u_hasSceneColor"))
            sh->setUniform1i("u_hasSceneColor", frame.hasSceneColor);
        // Diagnostic view selectors. Stripped from the unlit shader's variant,
        // so check hasUniform. modeConfig.debugMode is 0 in non-diagnostic
        // modes; forceNeutralMaterial is 0 unless LightingOnly is active.
        if (sh->hasUniform("u_debugMode"))
            sh->setUniform1i("u_debugMode", view.modeConfig.debugMode);
        if (sh->hasUniform("u_forceNeutralMaterial"))
            sh->setUniform1i("u_forceNeutralMaterial",
                view.modeConfig.forceNeutralMaterial ? 1 : 0);
        // Camera clip range for the Depth diagnostic. Pushed unconditionally;
        // the shader only reads it when u_debugMode == 2.
        if (sh->hasUniform("u_zNear"))
            sh->setUniform1f("u_zNear", view.camera.zNear);
        if (sh->hasUniform("u_zFar"))
            sh->setUniform1f("u_zFar", view.camera.zFar);
    };

    // CameraBlock and LightsBlock UBOs are owned by GLView and bound once
    // per frame in sync().

    GLShader*      currentShader   = nullptr;
    MaterialType   currentType     = MaterialType::Opaque;
    MaterialHandle currentMaterial = {};

    // Which material classes this pass draws.
    auto inPhase = [&](MaterialType t) {
        if (m_phase == Phase::Opaque)      return t != MaterialType::Transparent;
        if (m_phase == Phase::Transparent) return t == MaterialType::Transparent;
        return true;  // All (legacy single pass)
    };

    // Per-batch OIT routing precomputed once per frame: true means the batch
    // belongs to the OIT sub-loop, false to the sorted sub-loop. OIT only
    // kicks in when the global toggle is on, the batch is Transparent, the
    // material opts in, and the material isn't transmissive (OIT can't
    // reproduce screen-space refraction so glass / liquid stays on the
    // sorted path regardless of useOIT). Both sub-loops index by batch
    // position so the route lookup is O(1) and the GLMaterial hashmap query
    // happens once per batch instead of three times.
    std::vector<bool> oitRoute(batches.size(), false);

    // Dedicated transparent pass: the opaque geometry AND the skybox have
    // already been drawn into the HDR target by earlier passes. Two sub-loops
    // run here when the OIT toggle is on - first the sorted-blend batches
    // (back-to-front, scene-color snapshot for refraction), then the OIT
    // batches (additive accum to a separate FBO). With OIT off everything
    // funnels through the sorted loop, which is the legacy single-loop path.
    bool anyTransparent      = false;
    bool anySortedTransparent = false;  // transparent batches NOT routed to OIT
    bool anyOITTransparent    = false;  // transparent batches routed to OIT
    if (m_phase == Phase::Transparent) {
        for (size_t i = 0; i < batches.size(); ++i) {
            const auto& b = batches[i];
            if (b.materialType != MaterialType::Transparent) continue;
            anyTransparent = true;
            bool toOIT = false;
            if (oitActive) {
                const GLMaterial* mm = glView.getMaterial(b.material);
                toOIT = mm
                     && mm->getUseOIT()
                     && !(mm->getFeatureFlags() & toBits(MaterialFeature::Transmission));
            }
            oitRoute[i] = toOIT;
            if (toOIT) anyOITTransparent = true;
            else       anySortedTransparent = true;
        }
        // No transparent geometry: still continue if we owe the wireframe
        // overlay draw at the end. The main batch loop's inPhase() filter
        // makes the no-transparent case a zero-iteration loop, so it is safe
        // to fall through; the post-loop overlay block handles the draw.
        if (!anyTransparent && !view.modeConfig.wireframeOverlay) return;

        // OIT resolve pass runs unconditionally when the global toggle is on.
        // Clear the OIT FBO here so a frame with no OIT-routed batches still
        // resolves cleanly (accum = 0, revealage = 1 -> no-op composite).
        if (oitActive && oit && !anyOITTransparent) {
            oit->bindForRender();
            hdrT.bindForRender();
        }

        if (anyTransparent && overdraw) {
            // Overdraw blend/depth state is already set above; suppress the
            // in-loop opaque -> transparent transition so it doesn't reset
            // depth-write / blend func to the regular transparent path.
            currentType = MaterialType::Transparent;
        }

        // Sorted sub-loop setup. Runs whenever there are any sorted (non-OIT)
        // transparents; OIT-only frames skip this entirely and the OIT setup
        // below takes over.
        if (anySortedTransparent && !overdraw) {
            hdrT.resolve();
            hdrT.bindForRender();
            hdrT.bindResolvedColor(GLConfig::TextureSlots::SceneColor);
            frame.hasSceneColor = 1;
            glContext.setBlending(true);
            glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glContext.setDepthWrite(false);
            // Cull back faces so a closed transmissive mesh shows only its
            // front surface (engine default is no culling + depth-write to
            // hide back faces; depth-write is off here, so without this you
            // see through to the inside / far faces of glass).
            glContext.setFaceCulling(true);
            glContext.setCullFace(GL_BACK);
            currentType = MaterialType::Transparent;
        }
    }

    // Index of the last Transparent batch - used to skip a wasted resnapshot
    // after the final glass draw in the transparent phase. (Always -1 when
    // there are no transparent batches; the loop simply never resnapshots.)
    size_t lastTransparentIdx = static_cast<size_t>(-1);
    if (m_phase == Phase::Transparent) {
        for (size_t i = 0; i < batches.size(); ++i)
            if (batches[i].materialType == MaterialType::Transparent)
                lastTransparentIdx = i;
    }

    // Coarse per-frame variant key pieces (light-count bucket + shadow-kind
    // mask), OR'd into per-batch material flags when resolving each variant.
    const LightShadowKey vkey = computeLightShadowKey(view);
    const uint8_t lightCountBucket = vkey.lightCountBucket;
    const uint8_t shadowKindMask   = vkey.shadowKindMask;

    // Per-batch render: shader variant resolve, material UBO/texture bind,
    // u_batchId push, draw. Shared between the sorted main loop and the OIT
    // sub-loop below; the only difference is the OIT_PASS variant flag, so
    // the lambda takes it as a parameter. State management (currentShader /
    // currentMaterial caches, polygon mode, blend / depth-test) stays at
    // the call sites - those depend on phase / overdraw / OIT FBO setup
    // and don't belong in the per-batch hot path.
    auto renderBatch = [&](size_t i, bool oitPass) {
        const auto& batch = batches[i];

        // Diagnostic modes route every batch through the unlit shader so the
        // geometry isn't AO-darkened, IBL-lit, or shadowed. modeConfig.
        // forceUnlit captures this for wireframe today; a future NormalsView
        // mode would use a normals shader the same way.
        const GLMaterial* material = glView.getMaterial(batch.material);
        ShaderHandle baseHandle = view.modeConfig.forceUnlit
            ? m_shaders[static_cast<int>(MaterialType::Unlit)]
            : m_shaders[static_cast<int>(batch.materialType)];
        if (!baseHandle) baseHandle = m_shaders[static_cast<int>(MaterialType::Opaque)];
        const ShaderAsset& shaderAsset = resources.get(baseHandle);
        GLShader* shader = nullptr;
        if (shaderAsset.variantAware) {
            GLView::ShaderVariantKey key;
            key.materialFlags    = material ? material->getFeatureFlags() : 0u;
            key.lightCountBucket = lightCountBucket;
            key.shadowKindMask   = shadowKindMask;
            key.oitPass          = oitPass;
            shader = glView.resolveShaderVariant(baseHandle, key, resources);
        } else {
            shader = glView.resolveShader(baseHandle, resources);
        }
        if (!shader) return;

        if (shader != currentShader) {
            shader->bind();
            applyFrameUniforms(shader);
            currentShader = shader;
        }

        if (batch.material && batch.material != currentMaterial) {
            if (material) {
                material->bind(GLConfig::UBOBindingPoints::Material);
                material->bindTextures(glView);
                currentMaterial = batch.material;
            } else {
                LOG_WARNING("Failed to get material for batch (skipping material bind)");
            }
        }

        // BatchId diagnostic: push the loop index so the PBR shader can hash
        // it to a per-batch colour. Stripped from non-debug variants by the
        // GLSL compiler; hasUniform handles that without log spam.
        if (shader->hasUniform("u_batchId"))
            shader->setUniform1i("u_batchId", static_cast<int>(i));

        GLMesh* mesh = glView.getMutableMesh(batch.mesh);
        if (mesh) {
            batcher.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
            mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.instanceCount, batch.firstInstance);
        } else {
            LOG_WARNING("Failed to get mesh for batch (skipping draw call)");
        }
    };

    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];

        if (!inPhase(batch.materialType)) continue;

        // OIT-routed batches sit out the sorted loop and render in the
        // dedicated OIT pass below. Only the transparent phase has routing
        // to consider; opaque batches are unaffected.
        if (m_phase == Phase::Transparent && oitRoute[i]) continue;

        // Material-type transition: blend/depth state + the opaque-scene
        // snapshot. Type drives this (not shader identity) because per-
        // material variants make the shader change within a type too.
        // Overdraw mode pins blend state for the whole pass, so the
        // type-transition state writes are suppressed here too.
        if (batch.materialType != currentType && !overdraw) {
            if (currentType == MaterialType::Transparent && batch.materialType != MaterialType::Transparent) {
                glContext.setDepthWrite(true);
                glContext.setBlending(false);
                glContext.setFaceCulling(false);
                frame.hasSceneColor = 0;
            }

            if (batch.materialType == MaterialType::Transparent && currentType != MaterialType::Transparent) {
                // First transparent batch: snapshot the opaque-only scene so
                // transmissive materials refract what is actually behind them
                // (resolve MSAA -> single-sample, rebind the MSAA target for
                // the upcoming transparent draws, bind the copy for sampling).
                hdrT.resolve();
                hdrT.bindForRender();
                hdrT.bindResolvedColor(GLConfig::TextureSlots::SceneColor);
                frame.hasSceneColor = 1;

                // currentShader is the last opaque variant. Re-apply the
                // new u_hasSceneColor immediately so any glass batch that
                // happens to bind the same shader (unlikely with variants
                // but possible if their feature flags collide) sees the
                // updated value without a redundant rebind.
                if (currentShader) applyFrameUniforms(currentShader);

                glContext.setBlending(true);
                glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glContext.setDepthWrite(false);
                glContext.setFaceCulling(true);
                glContext.setCullFace(GL_BACK);
            }

            currentType = batch.materialType;
        }

        renderBatch(i, /*oitPass=*/false);

        // Per-batch refresh of the opaque-scene snapshot for layered glass:
        // after this transparent batch has rendered, re-resolve the HDR target
        // so u_sceneColor now contains opaque + sky + every transparent batch
        // drawn so far. The next (closer) batch's screen-space refraction then
        // refracts what's actually behind it - including farther glass.
        //
        // Only the next-consumer batch that actually samples u_sceneColor
        // needs this snapshot - i.e. one with the Transmission feature.
        // A scene of plain alpha-blended particles, or a single piece of
        // glass with non-transmissive transparents around it, pays nothing
        // here; only multi-layer glass triggers the per-batch resolve.
        if (m_phase == Phase::Transparent && i < lastTransparentIdx) {
            bool nextNeedsSnapshot = false;
            for (size_t j = i + 1; j <= lastTransparentIdx; ++j) {
                if (batches[j].materialType != MaterialType::Transparent) continue;
                const GLMaterial* next = glView.getMaterial(batches[j].material);
                if (next && (next->getFeatureFlags() & toBits(MaterialFeature::Transmission))) {
                    nextNeedsSnapshot = true;
                }
                break;  // each later transmissive batch will trigger its own resolve via this same check
            }
            if (nextNeedsSnapshot) {
                hdrT.resolve();
                hdrT.bindForRender();
            }
        }
    }

    // OIT sub-loop: batches routed to OIT render here, after the sorted-blend
    // loop above. Separate FBO + per-attachment blend funcs so the additive
    // accum + multiplicative revealage outputs land in their own targets;
    // the OIT resolve pass composites the result onto the HDR scene
    // (which already contains opaque + sky + the sorted transparents above).
    if (m_phase == Phase::Transparent && anyOITTransparent && !overdraw && oit) {
        // Blit opaque-phase depth from the MSAA HDR FBO into the OIT FBO's
        // single-sample depth buffer so OIT transparents still depth-test
        // against the opaque scene. Sorted transparents already wrote into
        // the MSAA depth above; that's fine - the blit grabs the latest
        // depth state which includes them.
        oit->copyDepthFrom(hdrT.msFboId(), hdrT.width(), hdrT.height());
        oit->bindForRender();
        glContext.setDepthTest(true);
        glContext.setDepthWrite(false);
        glContext.setBlending(true);
        glBlendFunci(0, GL_ONE, GL_ONE);                       // accum:    additive
        glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);      // revealage: multiplicative
        frame.hasSceneColor = 0;
        glContext.setFaceCulling(true);
        glContext.setCullFace(GL_BACK);

        // Force shader + material rebind so the variant lookup picks the
        // OIT_PASS variant for the same material a sorted batch may have
        // already bound. The state we set above is the OIT-buffer state;
        // the loop below doesn't change it.
        currentShader   = nullptr;
        currentMaterial = MaterialHandle{};
        currentType     = MaterialType::Transparent;

        for (size_t i = 0; i < batches.size(); ++i) {
            if (!oitRoute[i]) continue;
            renderBatch(i, /*oitPass=*/true);
        }

        // Rebind the HDR FBO so any post-loop work (wireframe overlay, the
        // standard end-of-pass restore) and later passes see the scene target
        // rather than the OIT FBO.
        hdrT.bindForRender();
    }

    // Restore GL state if we ended in transparent mode (back to the engine
    // default: no face culling, depth-write on, blending off). Polygon mode
    // restore happens in PolygonModeGuard above on any return path.
    if (currentType == MaterialType::Transparent) {
        glContext.setDepthWrite(true);
        glContext.setBlending(false);
        glContext.setFaceCulling(false);
    }

    // Overdraw diagnostic restores blend + depth-test + depth-write to the
    // engine default so downstream passes (skybox, post, composite, ImGui)
    // see a clean state.
    if (overdraw) {
        glContext.setBlending(false);
        glContext.setDepthTest(true);
        glContext.setDepthWrite(true);
    }

    // WireframeOverShaded: after the shaded scene is composed, run a second
    // draw that puts every visible batch into the overlay attachment as line
    // geometry. Polygon offset pulls the wires forward so they sit visibly
    // above the fill. Only the Transparent phase does this (it runs last;
    // opaque overlays would be overwritten by skybox + transparent draws).
    //
    // The overlay attachment is the same path AABB / Grid use: composite
    // blends it on top of the tonemapped scene with no display transform, so
    // the wires read as flat lines and not bloomed/exposed/tonemapped along
    // with the HDR shading. The unlit shader is bound with u_lineOverlay = 1
    // so every line writes a fixed light colour regardless of the material.
    if (m_phase == Phase::Transparent && view.modeConfig.wireframeOverlay) {
        drawWireframeOverlay(gl, glView, view, resources, hdrT,
                             m_shaders[static_cast<int>(MaterialType::Unlit)],
                             m_shaders[static_cast<int>(MaterialType::Opaque)],
                             applyFrameUniforms);
    }
}

} // namespace Engine
