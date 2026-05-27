#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_forward_pass.h"

#include <string>

#include <GL/glew.h>

#include "logger.h"

#include "core/gl_backend.h"
#include "core/gl_instance_batcher.h"
#include "core/gl_scene_target.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_ibl.h"
#include "resource/gl_material.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_shadow_map.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {

/**
 * @brief RAII guard: restore GL_FILL polygon mode on destruction. Used to wrap
 *
 * the wireframe set/restore around the body of execute() so every return
 * path (including the transparent-phase "no transparent batches" early
 * exit) leaves polygon mode back at GL_FILL - otherwise post passes
 * rasterize as line segments and ImGui draws as outlines.
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
}

GLForwardPass::GLForwardPass(ShaderHandle pbrShader, Phase phase)
    : RenderPass(std::string("GLForwardPass") + phaseSuffix(phase)), m_phase(phase) {
    m_shaders[static_cast<int>(MaterialType::Opaque)]      = pbrShader;
    m_shaders[static_cast<int>(MaterialType::Transparent)] = pbrShader;
    m_shaders[static_cast<int>(MaterialType::AlphaMask)]   = pbrShader;
    // Unlit stays empty until setShader() is called
}

void GLForwardPass::setShader(MaterialType type, ShaderHandle shader) {
    m_shaders[static_cast<int>(type)] = shader;
}

void GLForwardPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for forward pass
}

void GLForwardPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLForwardPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    // Graph-registered transient resources. Persistent backend state
    // (GLView / GLContext / default target) still comes through gl.
    auto& hdrT       = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& gbuffer    = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& ibl        = *rg.resource<GLIBL>(RGResource::IBL);
    auto& shadowAtlas = *rg.resource<GLShadowAtlas>(RGResource::ShadowAtlas);

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

    // Bind both shadow atlases for the PBR shader to sample.
    shadowAtlas.bind2DForReading(GLConfig::TextureSlots::ShadowMap2D);
    shadowAtlas.bindCubeForReading(GLConfig::TextureSlots::ShadowMapCube);

    // Bind the baked IBL set (irradiance / prefilter / BRDF LUT) and tell the
    // PBR shader whether to use it. Falls back to flat ambient when no bake.
    const bool iblReady = ibl.isReady();
    if (iblReady) {
        ibl.bindIrradiance(GLConfig::TextureSlots::IrradianceMap);
        ibl.bindPrefilter(GLConfig::TextureSlots::PrefilterMap);
        ibl.bindBrdf(GLConfig::TextureSlots::BrdfLUT);
        // Raw env cube too: the PBR shader blends a sharp env reflection in
        // at low roughness so polished metal reads as a true mirror, not the
        // prefilter's mip-0 GGX blur.
        ibl.bindEnvCube(GLConfig::TextureSlots::EnvCube);
    }
    // Screen-space AO from the prepass/GTAO (slot SSAO); enabled when both
    // the G-buffer is live and the environment toggle is on.
    const bool ssaoOn = gbuffer.isReady() && view.environment.ao.enabled;
    gbuffer.bindOcclusion(GLConfig::TextureSlots::SSAO);

    // Per-material shader variants mean we no longer have a single PBR
    // program to set frame-wide uniforms on up front. Capture them in a
    // local snapshot and re-apply whenever we bind a new variant - the
    // GL_INVALID_OPERATION-safe path is to just call setUniform after each
    // bind (locations are cached and missing names resolve to -1 no-op).
    struct PBRFrameUniforms {
        int   hasIBL;
        float iblIntensity;
        int   ssaoEnabled;
        float screenW;
        float screenH;
        int   hasSceneColor;  // flips 0 -> 1 at the opaque/transparent boundary
    };
    PBRFrameUniforms frame{
        iblReady ? 1 : 0,
        view.environment.ibl.intensity,
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
        sh->setUniform1i("u_ssaoEnabled", frame.ssaoEnabled);
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

    // Dedicated transparent pass: the opaque geometry AND the skybox have
    // already been drawn into the HDR target by earlier passes. Snapshot
    // that as the scene-behind source, then set transparent GL state once
    // up front (the in-loop type transition below only drives Phase::All).
    // We don't pre-bind a PBR program here any more - each batch picks its
    // own variant below and applyFrameUniforms() pushes u_hasSceneColor=1
    // into that variant when it binds.
    bool anyTransparent = false;
    if (m_phase == Phase::Transparent) {
        for (const auto& b : batches)
            if (b.materialType == MaterialType::Transparent) { anyTransparent = true; break; }
        // No transparent geometry: still continue if we owe the wireframe
        // overlay draw at the end. The main batch loop's inPhase() filter
        // makes the no-transparent case a zero-iteration loop, so it is safe
        // to fall through; the post-loop overlay block handles the draw.
        if (!anyTransparent && !view.modeConfig.wireframeOverlay) return;

        if (anyTransparent) {
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

    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];

        if (!inPhase(batch.materialType)) continue;

        // Material-type transition: blend/depth state + the opaque-scene
        // snapshot. Type drives this (not shader identity) because per-
        // material variants make the shader change within a type too.
        if (batch.materialType != currentType) {
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

        // Resolve the shader for this batch. Variant-aware shaders (today:
        // pbr) go through the per-material variant cache so each material
        // gets a program with only its features compiled in. Variant-
        // unaware shaders (unlit, ...) share one program across materials -
        // routing them through the variant cache would compile redundant
        // identical programs since their source doesn't reference HAS_X.
        // Falls back to the Opaque slot's shader when the type slot is
        // empty (e.g. AlphaMask not wired up by the caller).
        const GLMaterial* material = glView.getMaterial(batch.material);
        // Diagnostic modes route every batch through the unlit shader so
        // the geometry isn't AO-darkened, IBL-lit, or shadowed. modeConfig.
        // forceUnlit captures this for wireframe today; a future
        // NormalsView mode would use a normals shader the same way (a
        // new bool in the config or a shader override field).
        ShaderHandle baseHandle = view.modeConfig.forceUnlit
            ? m_shaders[static_cast<int>(MaterialType::Unlit)]
            : m_shaders[static_cast<int>(batch.materialType)];
        if (!baseHandle) baseHandle = m_shaders[static_cast<int>(MaterialType::Opaque)];
        const ShaderAsset& shaderAsset = resources.get(baseHandle);
        GLShader* shader = nullptr;
        if (shaderAsset.variantAware) {
            const uint32_t flags = material ? material->getFeatureFlags() : 0u;
            shader = glView.resolveShaderVariant(baseHandle, flags, resources);
        } else {
            shader = glView.resolveShader(baseHandle, resources);
        }
        if (!shader) continue;

        if (shader != currentShader) {
            shader->bind();
            applyFrameUniforms(shader);
            currentShader = shader;
        }

        // Bind material (UBO + textures) - skip when identical to previous batch
        if (batch.material && batch.material != currentMaterial) {
            if (material) {
                material->bind(GLConfig::UBOBindingPoints::Material);
                material->bindTextures(glView);
                currentMaterial = batch.material;
            } else {
                LOG_WARNING("Failed to get material for batch (skipping material bind)");
            }
        }

        // Get mesh, attach shared instance buffer to its VAO (cached / no-op on
        // repeat), then issue a base-instance draw that reads from the right offset.
        GLMesh* mesh = glView.getMutableMesh(batch.mesh);

        if (mesh) {
            batcher.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
            mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.instanceCount, batch.firstInstance);
        } else {
            LOG_WARNING("Failed to get mesh for batch (skipping draw call)");
        }

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

    // Restore GL state if we ended in transparent mode (back to the engine
    // default: no face culling, depth-write on, blending off). Polygon mode
    // restore happens in PolygonModeGuard above on any return path.
    if (currentType == MaterialType::Transparent) {
        glContext.setDepthWrite(true);
        glContext.setBlending(false);
        glContext.setFaceCulling(false);
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
        ShaderHandle unlit = m_shaders[static_cast<int>(MaterialType::Unlit)];
        if (!unlit) unlit = m_shaders[static_cast<int>(MaterialType::Opaque)];
        GLShader* lineShader = glView.resolveShader(unlit, resources);
        if (lineShader) {
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

            // Restore HDR-only routing so any downstream pass that re-uses the
            // FBO sees the engine default (color attachment 0).
            hdrT.bindForRender();
        }
    }
}

} // namespace Engine
