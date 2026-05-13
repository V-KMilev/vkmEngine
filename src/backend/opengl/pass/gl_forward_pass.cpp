#include "gl_forward_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "gl_shader.h"
#include "resource/gl_mesh.h"
#include "resource/gl_material.h"
#include "resource/gl_instance_buffer.h"
#include "core/gl_instance_batcher.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

void GLForwardPass::setupSamplers(Core::Shader& shader) {
    shader.bind();
    shader.setUniform1i(GLConfig::UniformNames::AlbedoTexture, GLConfig::TextureSlots::Albedo);
    shader.setUniform1i(GLConfig::UniformNames::NormalTexture, GLConfig::TextureSlots::Normal);
    shader.setUniform1i(GLConfig::UniformNames::MetallicRoughnessTexture, GLConfig::TextureSlots::MetallicRoughness);
    shader.setUniform1i(GLConfig::UniformNames::AOMetallicRoughnessTexture, GLConfig::TextureSlots::MetallicRoughness);
    shader.setUniform1i(GLConfig::UniformNames::AOTexture, GLConfig::TextureSlots::AO);
    shader.setUniform1i(GLConfig::UniformNames::EmissionTexture, GLConfig::TextureSlots::Emission);
    shader.setUniform1i(GLConfig::UniformNames::HeightTexture, GLConfig::TextureSlots::Height);
    shader.setUniform1i(GLConfig::UniformNames::ClearcoatTexture, GLConfig::TextureSlots::Clearcoat);
    shader.setUniform1i(GLConfig::UniformNames::TransmissionTexture, GLConfig::TextureSlots::Transmission);
    shader.setUniform1i(GLConfig::UniformNames::MetallicTexture, GLConfig::TextureSlots::Metallic);
    shader.setUniform1i(GLConfig::UniformNames::RoughnessTexture, GLConfig::TextureSlots::Roughness);
}

GLForwardPass::GLForwardPass(Core::Shader& pbrShader) : RenderPass("GLForwardPass") {
    m_shaders[static_cast<int>(MaterialType::Opaque)]      = &pbrShader;
    m_shaders[static_cast<int>(MaterialType::Transparent)]  = &pbrShader;
    // Unlit stays nullptr until setShader() is called

    setupSamplers(pbrShader);
}

void GLForwardPass::setShader(MaterialType type, Core::Shader& shader) {
    m_shaders[static_cast<int>(type)] = &shader;
    setupSamplers(shader);
}

void GLForwardPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for forward pass
}

void GLForwardPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLForwardPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    glContext.setClearColor(view.environment.clearColor);
    glContext.clearColor();
    glContext.clear();

    if (view.drawables.empty()) {
        return;
    }

    auto& glView = gl.getView();

    // Resource sync happens in RenderBackend::syncResources before pass execution.
    glView.buildInstanceBatches(view);

    auto& batcher = glView.getInstanceBatcher();
    const auto& batches = batcher.getBatches();
    auto& instanceBuffer = batcher.getBuffer();

    // CameraBlock and LightsBlock UBOs are owned by GLView and bound once
    // per frame in sync().

    Core::Shader* currentShader = nullptr;
    MaterialType currentType = MaterialType::Opaque;
    MaterialHandle currentMaterial{};

    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];

        // Switch shader when material type changes
        Core::Shader* shader = m_shaders[static_cast<int>(batch.materialType)];
        if (!shader) {
            // Fall back to opaque PBR shader if variant not set
            shader = m_shaders[static_cast<int>(MaterialType::Opaque)];
        }

        if (shader != currentShader) {
            // Transition GL state between material type groups
            if (currentType == MaterialType::Transparent && batch.materialType != MaterialType::Transparent) {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }

            if (batch.materialType == MaterialType::Transparent && currentType != MaterialType::Transparent) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
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
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

} // namespace Engine
