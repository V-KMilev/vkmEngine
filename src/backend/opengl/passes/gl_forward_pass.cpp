#include "gl_forward_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_backend.h"
#include "gl_shader.h"
#include "gl_mesh.h"
#include "gl_material.h"

#include "render_view.h"
#include "resource_manager.h"

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

    // Bind shader and set global uniforms
    m_shader.bind();

    // Bind lights UBO
    glView.getLights().bind(GLConfig::UBOBindingPoints::Lights);

    // Set camera uniforms
    m_shader.setUniform3fv(GLConfig::UniformNames::CameraPosition, view.camera.position);
    m_shader.setUniformMatrix4fv(GLConfig::UniformNames::View, view.camera.view);
    m_shader.setUniformMatrix4fv(GLConfig::UniformNames::Projection, view.camera.projection);
    m_shader.setUniformMatrix4fv(GLConfig::UniformNames::ViewProjection, view.camera.viewProjection);

    // Draw all visible meshes
    for (const auto& drawable : view.drawables) {
        // Set per-object model matrix
        m_shader.setUniformMatrix4fv(GLConfig::UniformNames::Model, drawable.model);

        // Bind material if present
        if (drawable.material) {
            const GLMaterial* material = glView.getMaterial(drawable.material);
            if (material) {
                material->bind(GLConfig::UBOBindingPoints::Material);
                material->bindTextures(glView);
            } else {
                LOG_WARNING("Failed to get material for drawable (skipping material bind)");
            }
        }

        // Get and draw mesh
        const GLMesh* mesh = glView.getMesh(drawable.mesh);
        if (mesh) {
            mesh->draw(GL_TRIANGLES);
        } else {
            LOG_WARNING("Failed to get mesh for drawable (skipping draw call)");
        }
    }
}

} // namespace Engine
