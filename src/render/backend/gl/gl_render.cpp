#include "gl_render.h"

#include "gl_context.h"
#include "gl_shader.h"

#include "render_view.h"
#include "resource_manager.h"

namespace Engine {

GLRender::GLRender(
    Core::Context& context,
    Core::Shader& shader
) : RenderBackend(RenderBackendType::OpenGL),
    m_context(context),
    m_shader(shader) {}

void GLRender::resize(uint32_t width, uint32_t height) {
    m_context.setViewport(0, 0, width, height);
}

void GLRender::render(
    const RenderView& renderView,
    const ResourceManager& resourceManager,
    uint32_t width,
    uint32_t height
) {
    m_view.syncMeshes(renderView, resourceManager);

    m_context.clearColor();
    m_context.clear();

    m_shader.bind();

    m_shader.setUniformMatrix4fv("u_viewProjection", renderView.camera.viewProjection);

    for (const auto& instance : renderView.instances) {
        if (!instance.visible) {
            continue;
        }
        if (!instance.mesh) {
            continue;
        }

        m_shader.setUniformMatrix4fv("u_model", instance.model);

        const GLMesh& mesh = m_view.getMesh(instance.mesh);
        mesh.draw();
    }
}

} // namespace Engine