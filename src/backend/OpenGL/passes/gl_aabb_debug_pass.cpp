#include "gl_aabb_debug_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_context.h"
#include "gl_shader.h"

#include "gl_backend.h"
#include "gl_mesh.h"

#include "render_view.h"
#include "resource_manager.h"
#include "frustum_culler.h"
#include "mesh_asset.h"

namespace Engine {

GLAABBDebugPass::GLAABBDebugPass(
    Core::Shader& shader,
    const glm::vec3& color
) : RenderPass("GLAABBDebugPass"),
    m_shader(shader),
    m_color(color) {
    initializeWireframeCube();
}

void GLAABBDebugPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) { /* Nothing to do */ }

void GLAABBDebugPass::initializeWireframeCube() {
    // Create a wireframe cube mesh (8 corners, 12 edges)
    MeshAsset wireframeMesh;

    // 8 corner vertices (unit cube from -0.5 to 0.5)
    wireframeMesh.vertices = {
        Vertex{ glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 0: bottom-left-front
        Vertex{ glm::vec3( 0.5f, -0.5f, -0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 1: bottom-right-front
        Vertex{ glm::vec3( 0.5f,  0.5f, -0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 2: top-right-front
        Vertex{ glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 3: top-left-front
        Vertex{ glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 4: bottom-left-back
        Vertex{ glm::vec3( 0.5f, -0.5f,  0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 5: bottom-right-back
        Vertex{ glm::vec3( 0.5f,  0.5f,  0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) },  // 6: top-right-back
        Vertex{ glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec3(0), glm::vec2(0), glm::vec4(0) }   // 7: top-left-back
    };

    // 12 edges: 24 indices (2 per edge)
    // Each edge is defined exactly once to avoid duplicate lines
    wireframeMesh.indices = {
        // Bottom face edges (Y = -0.5)
        0, 1,  // bottom-front: left to right
        1, 5,  // bottom-right: front to back
        5, 4,  // bottom-back: right to left
        4, 0,  // bottom-left: back to front
        
        // Top face edges (Y = 0.5)
        3, 2,  // top-front: left to right
        2, 6,  // top-right: front to back
        6, 7,  // top-back: right to left
        7, 3,  // top-left: back to front
        
        // Vertical edges (connecting bottom to top)
        0, 3,  // left-front
        1, 2,  // right-front
        5, 6,  // right-back
        4, 7   // left-back
    };

    m_aabb = std::make_unique<GLMesh>(wireframeMesh);
}

void GLAABBDebugPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_WARNING("%s can only be used with OpenGL backend, got %s, skipping pass", getName().c_str(), toString(backend.getType()));
        return;
    }

    auto& gl = static_cast<GLBackend&>(backend);
    auto& glContext = gl.getContext();

    glContext.setFaceCulling(false);                       // Disable face culling for lines
    glContext.setPolygonMode(GL_FRONT_AND_BACK, GL_LINE);  // Ensure polygon mode is set to lines (though GL_LINES primitive shouldn't need this)

    m_shader.bind();

    m_shader.setUniformMatrix4fv("u_viewProjection", view.camera.viewProjection);
    m_shader.setUniform3fv("u_color", glm::vec3(1.0f, 0.0f, 0.0f));  // Red color for AABBs

    // Draw AABBs for all drawables
    for (const auto& drawable : view.drawables) {
        // Get mesh asset to access bounds
        const auto& meshAsset = resources.get(drawable.mesh);

        // Skip meshes without valid bounds
        if ((meshAsset.boundsMin.x == meshAsset.boundsMax.x) &&
            (meshAsset.boundsMin.y == meshAsset.boundsMax.y) &&
            (meshAsset.boundsMin.z == meshAsset.boundsMax.z)) {
            continue;
        }

        // Transform AABB from model space to world space
        glm::vec3 worldMin, worldMax;
        FrustumCuller::transformAABB(
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

        // Set model matrix and draw aabb cube
        m_shader.setUniformMatrix4fv("u_model", aabbModel);

        m_aabb->draw(GL_LINES);
    }

    glContext.setPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

} // namespace Engine
