#include "gl_grid_pass.h"

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

#include "generator/mesh_generators.h"

namespace Engine {

GLGridPass::GLGridPass(ShaderHandle shader)
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
    auto& glView    = gl.getView();

    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    glContext.setBlending(true);
    glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glContext.setDepthTest(true);
    glContext.setDepthWrite(false);

    glContext.setFaceCulling(false);

    shader->bind();
    STATS_RECORD_SHADER_SWITCH();

    // Read grid settings from environment config
    const auto& env = view.environment;

    // Center grid under camera in XZ, keep it on ground plane.
    glm::vec3 gridPos(view.camera.position.x, 0.0f, view.camera.position.z);

    glm::mat4 model(1.0f);
    model = glm::translate(model, gridPos);
    model = glm::scale(model, glm::vec3(env.gridSize));

    // CameraBlock UBO (binding 2) is bound by GLView for the frame.
    shader->setUniformMatrix4fv("u_model", model);
    shader->setUniform1f("u_gridScale", env.gridScale);
    shader->setUniform1f("u_gridFadeStart", env.gridFadeStart);
    shader->setUniform1f("u_gridFadeEnd", env.gridFadeEnd);

    m_mesh->draw(GL_TRIANGLES);

    glContext.setBlending(false);
    glContext.setDepthWrite(true);
}

void GLGridPass::initialize() {
    MeshAsset gridMesh = generatePlane(1.0f, 1.0f, 1, 1);
    m_mesh = std::make_unique<GLMesh>(gridMesh);
}

} // namespace Engine

