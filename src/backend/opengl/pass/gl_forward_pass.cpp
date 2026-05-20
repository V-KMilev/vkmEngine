#include "gl_forward_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_ibl.h"
#include "resource/gl_gbuffer.h"
#include "core/gl_instance_batcher.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

GLForwardPass::GLForwardPass(ShaderHandle pbrShader) : RenderPass("GLForwardPass") {
    m_shaders[static_cast<int>(MaterialType::Opaque)]      = pbrShader;
    m_shaders[static_cast<int>(MaterialType::Transparent)] = pbrShader;
    // Unlit stays empty until setShader() is called
}

void GLForwardPass::setShader(MaterialType type, ShaderHandle shader) {
    m_shaders[static_cast<int>(type)] = shader;
}

void GLForwardPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for forward pass
}

void GLForwardPass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLForwardPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    // The scene renders into the offscreen linear-HDR target so light is
    // never clamped before tone mapping. The composite pass resolves this
    // and applies exposure + AgX to the backbuffer.
    gl.getHdrTarget().bindForRender();

    // The opaque (or legacy All) pass owns the clear. The transparent pass
    // must NOT clear - it refracts the opaque+sky scene drawn before it.
    if (m_phase != Phase::Transparent) {
        glContext.setClearColor(view.environment.clearColor);
        glContext.clearColor();
        glContext.clear();
    }

    if (view.drawables.empty()) {
        return;
    }

    auto& glView = gl.getView();

    // Resolve every shader variant up-front (resolveShader picks up any
    // hot-reload version bumps and re-applies the asset's sampler bindings).
    GLShader* shaders[3] = {};
    for (size_t i = 0; i < 3; ++i) {
        shaders[i] = glView.resolveShader(m_shaders[i], resources);
    }

    auto& batcher = glView.getInstanceBatcher();
    const auto& batches = batcher.getBatches();

    // Bind both shadow atlases for the PBR shader to sample.
    auto& shadowAtlas = glView.getShadowAtlas();
    shadowAtlas.bind2DForReading(GLConfig::TextureSlots::ShadowMap2D);
    shadowAtlas.bindCubeForReading(GLConfig::TextureSlots::ShadowMapCube);

    // Bind the baked IBL set (irradiance / prefilter / BRDF LUT) and tell the
    // PBR shader whether to use it. Falls back to flat ambient when no bake.
    auto& ibl = glView.getIBL();
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
    auto& gbuffer = gl.getGBuffer();
    const bool ssaoOn = gbuffer.isReady() && view.environment.ssao;
    gbuffer.bindOcclusion(GLConfig::TextureSlots::SSAO);

    if (GLShader* pbr = shaders[static_cast<int>(MaterialType::Opaque)]) {
        pbr->bind();
        pbr->setUniform1i("u_hasIBL", iblReady ? 1 : 0);
        pbr->setUniform1f("u_iblIntensity", view.environment.iblIntensity);
        pbr->setUniform1i("u_ssaoEnabled", ssaoOn ? 1 : 0);
        // Half-res AO is sampled by normalized screen UV (see pbr frag).
        pbr->setUniform2f("u_screenSize",
            static_cast<float>(view.viewportWidth),
            static_cast<float>(view.viewportHeight));
        // No opaque-scene copy yet this frame: transmissive materials fall
        // back to IBL refraction until the opaque->transparent boundary.
        pbr->setUniform1i("u_hasSceneColor", 0);
    }

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
    if (m_phase == Phase::Transparent) {
        bool anyTransparent = false;
        for (const auto& b : batches)
            if (b.materialType == MaterialType::Transparent) { anyTransparent = true; break; }
        if (!anyTransparent) return;  // opaque+sky already in the HDR; nothing to add

        auto& hdrT = gl.getHdrTarget();
        hdrT.resolve();
        hdrT.bindForRender();
        hdrT.bindResolvedColor(GLConfig::TextureSlots::SceneColor);
        if (GLShader* pbrT = shaders[static_cast<int>(MaterialType::Opaque)]) {
            pbrT->bind();
            pbrT->setUniform1i("u_hasSceneColor", 1);
            currentShader = pbrT;
        }
        glContext.setBlending(true);
        glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glContext.setDepthWrite(false);
        // Cull back faces so a closed transmissive mesh shows only its front
        // surface (engine default is no culling + depth-write to hide back
        // faces; depth-write is off here, so without this you see through to
        // the inside / far faces of glass).
        glContext.setFaceCulling(true);
        glContext.setCullFace(GL_BACK);
        currentType = MaterialType::Transparent;
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

        // Pick the shader for this batch's material type, falling back to
        // the opaque PBR shader if the variant slot is empty.
        GLShader* shader = shaders[static_cast<int>(batch.materialType)];
        if (!shader) shader = shaders[static_cast<int>(MaterialType::Opaque)];
        if (!shader) continue;

        // Material-type transition (blend/depth state + the opaque-scene
        // snapshot). Driven by the material TYPE, not shader identity:
        // opaque and transparent share the PBR program, so a
        // shader-equality guard never fires at the opaque->transparent
        // boundary and transparent would render with opaque state.
        if (batch.materialType != currentType) {
            if (currentType == MaterialType::Transparent && batch.materialType != MaterialType::Transparent) {
                glContext.setDepthWrite(true);
                glContext.setBlending(false);
                glContext.setFaceCulling(false);
            }

            if (batch.materialType == MaterialType::Transparent && currentType != MaterialType::Transparent) {
                // First transparent batch: snapshot the opaque-only scene so
                // transmissive materials refract what is actually behind them
                // (resolve MSAA -> single-sample, rebind the MSAA target for
                // the upcoming transparent draws, bind the copy for sampling).
                auto& hdrT = gl.getHdrTarget();
                hdrT.resolve();
                hdrT.bindForRender();
                hdrT.bindResolvedColor(GLConfig::TextureSlots::SceneColor);
                if (GLShader* pbrT = shaders[static_cast<int>(MaterialType::Opaque)]) {
                    pbrT->bind();
                    pbrT->setUniform1i("u_hasSceneColor", 1);
                    currentShader = pbrT;  // we just bound it
                }

                glContext.setBlending(true);
                glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glContext.setDepthWrite(false);
                glContext.setFaceCulling(true);
                glContext.setCullFace(GL_BACK);
            }

            currentType = batch.materialType;
        }

        if (shader != currentShader) {
            shader->bind();
            STATS_RECORD_SHADER_SWITCH();
            currentShader = shader;
        }

        // Bind material (UBO + textures) — skip when identical to previous batch
        if (batch.material && batch.material != currentMaterial) {
            const GLMaterial* material = glView.getMaterial(batch.material);
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
        // refracts what's actually behind it - including farther glass. Skipped
        // after the last transparent (no consumer) and outside Phase::Transparent.
        if (m_phase == Phase::Transparent && i < lastTransparentIdx) {
            auto& hdrT = gl.getHdrTarget();
            hdrT.resolve();
            hdrT.bindForRender();
        }
    }

    // Restore GL state if we ended in transparent mode (back to the engine
    // default: no face culling, depth-write on, blending off).
    if (currentType == MaterialType::Transparent) {
        glContext.setDepthWrite(true);
        glContext.setBlending(false);
        glContext.setFaceCulling(false);
    }
}

} // namespace Engine
