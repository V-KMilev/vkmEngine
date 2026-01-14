#include "gl_grid_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_backend.h"
#include "gl_shader.h"
#include "gl_mesh.h"

#include "render_view.h"
#include "resource_manager.h"

#include "mesh_generators.h"

namespace Engine {

GLGridPass::GLGridPass(Core::Shader& shader)
    : RenderPass("GLGridPass")
    , m_shader(shader)
{
    initialize();
}

void GLGridPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for grid pass
}


void GLGridPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    // Validate backend type
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLGridPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    glContext.setBlending(true);
    glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glContext.setDepthTest(true);
    glContext.setDepthWrite(false);

    glContext.setFaceCulling(false);

    m_shader.bind();

    // Center grid under camera in XZ, keep it on ground plane.
    glm::vec3 gridPos(view.camera.position.x, 0.0f, view.camera.position.z);

    glm::mat4 model(1.0f);
    model = glm::translate(model, gridPos);
    model = glm::scale(model, glm::vec3(m_config.size));

    m_shader.setUniformMatrix4fv("u_model", model);
    m_shader.setUniformMatrix4fv("u_view", view.camera.view);
    m_shader.setUniformMatrix4fv("u_projection", view.camera.projection);

    // Grid params
    m_shader.setUniform1f("u_gridScale", m_config.scale);
    m_shader.setUniform1f("u_gridFadeStart", m_config.fadeStart);
    m_shader.setUniform1f("u_gridFadeEnd", m_config.fadeEnd);

    m_shader.setUniform3fv("u_cameraPos", view.camera.position);

    m_mesh->draw(GL_TRIANGLES);

    glContext.setBlending(false);
    glContext.setDepthWrite(true);
}

void GLGridPass::initialize() {
    MeshAsset gridMesh = generatePlane(1.0f, 1.0f, 1, 1);
    m_mesh = std::make_unique<GLMesh>(gridMesh);
}

} // namespace Engine

