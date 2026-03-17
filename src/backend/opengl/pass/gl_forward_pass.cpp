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

    glView.syncMeshes(view, resources);
    glView.syncMaterials(view, resources);
    glView.syncTextures(view, resources);
    glView.syncLights(view, resources);
    glView.purgeStaleIfNeeded(view);

    glView.buildInstanceBatches(view);

    auto& batcher = glView.getInstanceBatcher();
    const auto& batches = batcher.getBatches();

    // Bind lights UBO (shared across all shader variants)
    glView.getLights().bind(GLConfig::UBOBindingPoints::Lights);

    Core::Shader* currentShader = nullptr;
    MaterialType currentType = MaterialType::Opaque;

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

            shader->setUniform3fv(GLConfig::UniformNames::CameraPosition, view.camera.position);
            shader->setUniformMatrix4fv(GLConfig::UniformNames::ViewProjection, view.camera.viewProjection);
            shader->setUniform1f(GLConfig::UniformNames::Exposure, view.camera.exposure);
            shader->setUniform3fv(GLConfig::UniformNames::AmbientColor, view.environment.ambientColor);
            shader->setUniform1f(GLConfig::UniformNames::AmbientIntensity, view.environment.ambientIntensity);

            currentShader = shader;
            currentType = batch.materialType;
        }

        // Bind material (UBO + textures)
        if (batch.material) {
            const GLMaterial* material = glView.getMaterial(batch.material);
            if (material) {
                material->bind(GLConfig::UBOBindingPoints::Material);
                material->bindTextures(glView);
            } else {
                LOG_WARNING("Failed to get material for batch (skipping material bind)");
            }
        }

        // Get mesh and instance buffer, issue draw call
        GLMesh* mesh = glView.getMutableMesh(batch.mesh);
        GLInstanceBuffer* instanceBuffer = batcher.getInstanceBuffer(i);

        if (mesh && instanceBuffer) {
            instanceBuffer->attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
            mesh->drawInstanced(GL_TRIANGLES, batch.instanceCount);
        } else {
            LOG_WARNING("Failed to get mesh or instance buffer for batch (skipping draw call)");
        }
    }

    // Restore GL state if we ended in transparent mode
    if (currentType == MaterialType::Transparent) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

} // namespace Engine
