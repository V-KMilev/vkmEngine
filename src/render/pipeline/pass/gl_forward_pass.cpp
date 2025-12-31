#include "gl_forward_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_context.h"
#include "gl_shader.h"

#include "gl_backend.h"
#include "gl_mesh.h"
#include "gl_material.h"

#include "render_view.h"
#include "resource_manager.h"

namespace Engine {

GLForwardPass::GLForwardPass(Core::Shader& shader) : RenderPass("GLForwardPass"), m_shader(shader) {}

void GLForwardPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do
}

void GLForwardPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_WARNING("%s can only be used with OpenGL backend, got %s, skipping pass", getName().c_str(), toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);

    auto& glContext = gl.getContext();
    auto& glView = gl.getView();

    glView.syncMeshes(view, resources);
    glView.syncMaterials(view, resources);

    glContext.clearColor();
    glContext.clear();

    m_shader.bind();

    m_shader.setUniform3fv("u_cameraPosition", view.camera.position);
    m_shader.setUniformMatrix4fv("u_view", view.camera.view);
    m_shader.setUniformMatrix4fv("u_projection", view.camera.projection);
    m_shader.setUniformMatrix4fv("u_viewProjection", view.camera.viewProjection);

    for (const auto& drawable : view.drawables) {
        m_shader.setUniformMatrix4fv("u_model", drawable.model);

        // Bind material if present (binding point 0 for material UBO)
        if (drawable.material) {
            const GLMaterial& material = glView.getMaterial(drawable.material);
            material.bind(0);
        }

        const GLMesh& mesh = glView.getMesh(drawable.mesh);
        mesh.draw(GL_TRIANGLES);
    }
}

} // namespace Engine
