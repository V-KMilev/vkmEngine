#include "gl_forward_pass.h"

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "gl_backend.h"
#include "gl_shader.h"
#include "gl_mesh.h"
#include "gl_material.h"
#include "gl_instance_buffer.h"
#include "gl_instance_batcher.h"

#include "render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

GLForwardPass::GLForwardPass(Core::Shader& shader) : RenderPass("GLForwardPass"), m_shader(shader) {
    // Set up texture samplers once during initialization
    m_shader.bind();

    // Set sampler units to match TextureSlots in gl_config.h
    m_shader.setUniform1i(GLConfig::UniformNames::AlbedoTexture, GLConfig::TextureSlots::Albedo);
    m_shader.setUniform1i(GLConfig::UniformNames::NormalTexture, GLConfig::TextureSlots::Normal);
    m_shader.setUniform1i(GLConfig::UniformNames::MetallicRoughnessTexture, GLConfig::TextureSlots::MetallicRoughness);
    m_shader.setUniform1i(GLConfig::UniformNames::AOMetallicRoughnessTexture, GLConfig::TextureSlots::MetallicRoughness);
    m_shader.setUniform1i(GLConfig::UniformNames::AOTexture, GLConfig::TextureSlots::AO);
    m_shader.setUniform1i(GLConfig::UniformNames::EmissionTexture, GLConfig::TextureSlots::Emission);
    m_shader.setUniform1i(GLConfig::UniformNames::HeightTexture, GLConfig::TextureSlots::Height);
    m_shader.setUniform1i(GLConfig::UniformNames::ClearcoatTexture, GLConfig::TextureSlots::Clearcoat);
    m_shader.setUniform1i(GLConfig::UniformNames::TransmissionTexture, GLConfig::TextureSlots::Transmission);
    m_shader.setUniform1i(GLConfig::UniformNames::MetallicTexture, GLConfig::TextureSlots::Metallic);
    m_shader.setUniform1i(GLConfig::UniformNames::RoughnessTexture, GLConfig::TextureSlots::Roughness);
}

void GLForwardPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for forward pass
}

void GLForwardPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    // Validate backend type
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLForwardPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    // Clear framebuffer (always needed)
    glContext.clearColor();
    glContext.clear();

    // Early out if nothing to draw - skip all resource syncing and setup
    if (view.drawables.empty()) {
        return;
    }

    auto& glView = gl.getView();

    // Sync all resources with GPU
    glView.syncMeshes(view, resources);
    glView.syncMaterials(view, resources);
    glView.syncTextures(view, resources);
    glView.syncLights(view, resources);

    // Periodically purge GPU resources no longer referenced in the scene
    static uint32_t frameCounter = 0;
    if (++frameCounter >= 300) {
        glView.purgeStaleResources(view);
        frameCounter = 0;
    }

    // Bind shader and set global uniforms
    m_shader.bind();
    STATS_RECORD_SHADER_SWITCH();

    // Bind lights UBO
    glView.getLights().bind(GLConfig::UBOBindingPoints::Lights);

    // Set camera uniforms
    m_shader.setUniform3fv(GLConfig::UniformNames::CameraPosition, view.camera.position);
    m_shader.setUniformMatrix4fv(GLConfig::UniformNames::ViewProjection, view.camera.viewProjection);

    // Drawables are pre-sorted by (material, mesh). The batcher groups them
    // into batches where each batch shares the same mesh+material combo.
    // This reduces draw calls from O(entities) to O(unique mesh-material pairs).

    glView.buildInstanceBatches(view);

    auto& batcher = glView.getInstanceBatcher();
    const auto& batches = batcher.getBatches();

    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];

        // Bind material (UBO + textures) once per batch
        if (batch.material) {
            const GLMaterial* material = glView.getMaterial(batch.material);
            if (material) {
                material->bind(GLConfig::UBOBindingPoints::Material);
                material->bindTextures(glView);
            } else {
                LOG_WARNING("Failed to get material for batch (skipping material bind)");
            }
        }

        // Get mesh and its instance buffer
        GLMesh* mesh = glView.getMutableMesh(batch.mesh);
        GLInstanceBuffer* instanceBuffer = batcher.getInstanceBuffer(i);

        if (mesh && instanceBuffer) {
            // Attach instance buffer (model matrices) to VAO at locations 4-7
            instanceBuffer->attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);

            // Issue single instanced draw call for all instances in this batch
            mesh->drawInstanced(GL_TRIANGLES, batch.instanceCount);
        } else {
            LOG_WARNING("Failed to get mesh or instance buffer for batch (skipping draw call)");
        }
    }
}

} // namespace Engine
