#include "gl_forward_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_instance_buffer.h"
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

    glContext.setClearColor(view.environment.clearColor);
    glContext.clearColor();
    glContext.clear();

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
    auto& instanceBuffer = batcher.getBuffer();

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
    }

    // CameraBlock and LightsBlock UBOs are owned by GLView and bound once
    // per frame in sync().

    GLShader*      currentShader   = nullptr;
    MaterialType   currentType     = MaterialType::Opaque;
    MaterialHandle currentMaterial = {};

    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];

        // Pick the shader for this batch's material type, falling back to
        // the opaque PBR shader if the variant slot is empty.
        GLShader* shader = shaders[static_cast<int>(batch.materialType)];
        if (!shader) shader = shaders[static_cast<int>(MaterialType::Opaque)];
        if (!shader) continue;

        if (shader != currentShader) {
            // Transition GL state between material type groups
            if (currentType == MaterialType::Transparent && batch.materialType != MaterialType::Transparent) {
                glContext.setDepthWrite(true);
                glContext.setBlending(false);
            }

            if (batch.materialType == MaterialType::Transparent && currentType != MaterialType::Transparent) {
                glContext.setBlending(true);
                glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glContext.setDepthWrite(false);
            }

            shader->bind();
            STATS_RECORD_SHADER_SWITCH();

            currentShader = shader;
            currentType = batch.materialType;
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
            instanceBuffer.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
            mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.instanceCount, batch.firstInstance);
        } else {
            LOG_WARNING("Failed to get mesh for batch (skipping draw call)");
        }
    }

    // Restore GL state if we ended in transparent mode
    if (currentType == MaterialType::Transparent) {
        glContext.setDepthWrite(true);
        glContext.setBlending(false);
    }
}

} // namespace Engine
