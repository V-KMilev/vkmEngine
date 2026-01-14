#include "gl_navigation_gizmo_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_backend.h"
#include "gl_shader.h"
#include "gl_mesh.h"

#include "render_view.h"
#include "resource_manager.h"

#include "transform.h"
#include "mesh_generators.h"

namespace Engine {

GLNavigationGizmoPass::GLNavigationGizmoPass(Core::Shader& shader)
    : RenderPass("GLNavigationGizmoPass")
    , m_shader(shader)
{
    initialize();
}

void GLNavigationGizmoPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do for navigation gizmo pass
}

void GLNavigationGizmoPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    // Validate backend type
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLNavigationGizmoPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    int width = 1920;
    int height = 1080;

    // Calculate gizmo viewport position
    const float centerX = m_config.x * static_cast<float>(width);
    const float centerY = (1.0f - m_config.y) * static_cast<float>(height);

    int x = static_cast<int>(std::round(centerX - m_config.size * 0.5f));
    int y = static_cast<int>(std::round(centerY - m_config.size * 0.5f));
    int w = static_cast<int>(std::round(m_config.size));
    int h = static_cast<int>(std::round(m_config.size));

    // Clamp to valid bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > static_cast<int>(width)) { w = width - x; }
    if (y + h > static_cast<int>(height)) { h = height - y; }

    if (w <= 0 || h <= 0) {
        return;
    }

    // Save previous state
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Set gizmo viewport and state
    glContext.setDepthTest(false);
    glContext.setBlending(false);
    glContext.setDepthWrite(false);
    glContext.setViewport(x, y, w, h);

    m_shader.bind();

    glm::mat4 gizmoProj = glm::ortho(-m_config.scale, m_config.scale, -m_config.scale, m_config.scale, 0.1f, 10.0f);
    glm::mat4 gizmoView = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f));

    // Transform world axes to view space
    glm::mat3 viewRot = glm::mat3(view.camera.view);
    glm::vec3 worldX = glm::normalize(viewRot * Engine::WORLD_AXIS_X_RIGHT);
    glm::vec3 worldY = glm::normalize(viewRot * Engine::WORLD_AXIS_Y_UP);
    glm::vec3 worldZ = glm::normalize(viewRot * Engine::WORLD_AXIS_Z_FORWARD);

    m_shader.setUniformMatrix4fv("u_view", gizmoView);
    m_shader.setUniformMatrix4fv("u_projection", gizmoProj);

    auto directionToRotation = [](const glm::vec3& dir) -> glm::mat4 {
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), dir);
        float dot = glm::clamp(glm::dot(glm::vec3(1.0f, 0.0f, 0.0f), dir), -1.0f, 1.0f);
        float angle = std::acos(dot);

        if (glm::length(axis) < 0.001f) {
            return dot > 0.0f ? glm::mat4(1.0f) : glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        return glm::rotate(glm::mat4(1.0f), angle, glm::normalize(axis));
    };

    const float coneHalfHeight = 0.125f;
    const glm::mat4 coneRotation = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat4 translateToEnd = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f - coneHalfHeight, 0.0f, 0.0f));

    auto drawAxis = [&](const glm::vec3& worldDir, const glm::vec3& color) {
        glm::mat4 model = directionToRotation(worldDir);

        m_shader.setUniformMatrix4fv("u_model", model);
        m_shader.setUniform3fv("u_color", color);
        m_navAxisMesh->draw(GL_LINES);

        glm::mat4 arrowModel = model * translateToEnd * coneRotation;
        m_shader.setUniformMatrix4fv("u_model", arrowModel);
        m_shader.setUniform3fv("u_color", color);
        m_navArrowMesh->draw(GL_TRIANGLES);
    };

    drawAxis(worldX, {0.95f, 0.25f, 0.25f});
    drawAxis(worldY, {0.25f, 0.9f, 0.35f});
    drawAxis(worldZ, {0.3f, 0.5f, 0.95f});

    // Restore previous state
    glContext.setDepthTest(true);
    glContext.setBlending(true);
    glContext.setDepthWrite(true);
    glContext.setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void GLNavigationGizmoPass::initialize() {
    MeshAsset axisMesh;
    axisMesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {}, {}, {}},
        {{1.0f, 0.0f, 0.0f}, {}, {}, {}}
    };
    axisMesh.indices = { 0, 1 };
    axisMesh.computeAndSetBounds();
    m_navAxisMesh = std::make_unique<GLMesh>(axisMesh);

    MeshAsset arrowMesh = generateCone(0.08f, 0.25f, 16);
    m_navArrowMesh = std::make_unique<GLMesh>(arrowMesh);
}

} // namespace Engine
