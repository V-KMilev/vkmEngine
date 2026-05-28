#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_grid_pass.h"

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

#include "generator/mesh_generators.h"

namespace Engine {

bool GLGridPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.grid.enabled;
}

GLGridPass::GLGridPass(ShaderHandle shader)
    : RenderPass("GLGridPass")
    , m_shader(shader)
{
    initialize();
}

void GLGridPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {}


void GLGridPass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    // Validate backend type
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLGridPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    if (!view.environment.grid.enabled) return;

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();
    auto& glView    = gl.getView();

    // Route to the HDR FBO's overlay attachment so the grid's authored
    // colours skip the composite tonemap chain and show on screen exactly.
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    if (!hdr.isReady()) return;
    hdr.bindForOverlay();

    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    glContext.setBlending(true);
    glContext.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glContext.setDepthTest(true);
    glContext.setDepthWrite(false);

    glContext.setFaceCulling(false);

    shader->bind();

    // Read grid settings from environment config
    const auto& env = view.environment;

    // Center grid under camera in XZ, keep it on ground plane.
    glm::vec3 gridPos(view.camera.position.x, 0.0f, view.camera.position.z);

    glm::mat4 model(1.0f);
    model = glm::translate(model, gridPos);
    model = glm::scale(model, glm::vec3(env.grid.size));

    // CameraBlock UBO (binding 2) is bound by GLView for the frame.
    shader->setUniformMatrix4fv("u_model", model);
    shader->setUniform1f("u_gridScale", env.grid.scale);
    shader->setUniform1f("u_gridFadeStart", env.grid.fadeStart);
    shader->setUniform1f("u_gridFadeEnd", env.grid.fadeEnd);

    m_mesh->draw(GL_TRIANGLES);

    glContext.setBlending(false);
    glContext.setDepthWrite(true);
}

void GLGridPass::initialize() {
    MeshAsset gridMesh = generatePlane(1.0f, 1.0f, 1, 1);
    m_mesh = std::make_unique<GLMesh>(gridMesh);
}

} // namespace Engine

