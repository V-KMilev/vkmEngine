#include "gl_aabb_debug_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_backend.h"
#include "gl_shader.h"
#include "gl_mesh.h"

#include "render_view.h"
#include "resource_manager.h"

#include "bounds_utils.h"

namespace Engine {

GLAABBDebugPass::GLAABBDebugPass(
    Core::Shader& shader,
    const glm::vec3& color
) : RenderPass("GLAABBDebugPass"),
    m_shader(shader),
    m_color(color) {
    initialize();
}

void GLAABBDebugPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) { 
    // Nothing to do for AABB debug pass
}

void GLAABBDebugPass::initialize() {
    // Create a wireframe cube mesh (8 corners, 12 edges)
    MeshAsset wireframeMesh;

    // 8 corner vertices (unit cube from -0.5 to 0.5)
    wireframeMesh.vertices = {
        { {-0.5f, -0.5f, -0.5f}, {}, {}, {} }, // 0
        { { 0.5f, -0.5f, -0.5f}, {}, {}, {} }, // 1
        { { 0.5f,  0.5f, -0.5f}, {}, {}, {} }, // 2
        { {-0.5f,  0.5f, -0.5f}, {}, {}, {} }, // 3
        { {-0.5f, -0.5f,  0.5f}, {}, {}, {} }, // 4
        { { 0.5f, -0.5f,  0.5f}, {}, {}, {} }, // 5
        { { 0.5f,  0.5f,  0.5f}, {}, {}, {} }, // 6
        { {-0.5f,  0.5f,  0.5f}, {}, {}, {} }  // 7
    };

    // 12 edges → 24 indices (GL_LINES)
    wireframeMesh.indices = {
        // Bottom square
        0, 1,
        1, 5,
        5, 4,
        4, 0,

        // Top square
        3, 2,
        2, 6,
        6, 7,
        7, 3,

        // Vertical edges
        0, 3,
        1, 2,
        5, 6,
        4, 7
    };

    m_aabb = std::make_unique<GLMesh>(wireframeMesh);
}

void GLAABBDebugPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    // Validate backend type
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLAABBDebugPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    m_shader.bind();

    // Set global uniforms
    using namespace GLConfig::UniformNames;
    m_shader.setUniformMatrix4fv(ViewProjection, view.camera.viewProjection);
    m_shader.setUniform3fv(Color, m_color);

    // Draw AABBs for all visible drawables
    for (const auto& drawable : view.drawables) {
        // Get mesh asset to access bounds
        const auto& meshAsset = resources.get(drawable.mesh);

        // Skip meshes without valid bounds (degenerate AABBs)
        if ((meshAsset.boundsMin.x == meshAsset.boundsMax.x) &&
            (meshAsset.boundsMin.y == meshAsset.boundsMax.y) &&
            (meshAsset.boundsMin.z == meshAsset.boundsMax.z)) {
            continue;
        }

        // TODO: Check why when we rotate spheres the AABB is wrong
        // Transform AABB from model space to world space
        glm::vec3 worldMin, worldMax;
        localToWorldAABB(
            drawable.model,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            worldMin,
            worldMax
        );

        // Compute center and scale of the AABB
        glm::vec3 center = (worldMin + worldMax) * 0.5f;
        glm::vec3 size = worldMax - worldMin;

        // Build model matrix: translate to center, then scale
        glm::mat4 aabbModel = glm::mat4(1.0f);
        aabbModel = glm::translate(aabbModel, center);
        aabbModel = glm::scale(aabbModel, size);

        // Set model matrix and draw AABB wireframe
        m_shader.setUniformMatrix4fv(Model, aabbModel);
        m_aabb->draw(GL_LINES);
    }
}

} // namespace Engine
