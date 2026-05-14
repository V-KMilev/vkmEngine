#include "gl_aabb_debug_pass.h"

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

#include "system/visibility/bounds_utils.h"

namespace Engine {

GLAABBDebugPass::GLAABBDebugPass(ShaderHandle shader)
    : RenderPass("GLAABBDebugPass")
    , m_shader(shader)
{
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

    // 12 edges -> 24 indices (GL_LINES)
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
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLAABBDebugPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    m_modelScratch.clear();
    m_modelScratch.reserve(view.drawables.size());

    for (const auto& drawable : view.drawables) {
        const auto& meshAsset = resources.get(drawable.mesh);

        if ((meshAsset.boundsMin.x == meshAsset.boundsMax.x) &&
            (meshAsset.boundsMin.y == meshAsset.boundsMax.y) &&
            (meshAsset.boundsMin.z == meshAsset.boundsMax.z)) {
            continue;
        }

        glm::vec3 worldMin, worldMax;
        localToWorldAABB(
            drawable.model,
            meshAsset.boundsMin,
            meshAsset.boundsMax,
            worldMin,
            worldMax
        );

        const glm::vec3 center = (worldMin + worldMax) * 0.5f;
        const glm::vec3 size   = worldMax - worldMin;

        glm::mat4 aabbModel = glm::translate(glm::mat4(1.0f), center);
        aabbModel = glm::scale(aabbModel, size);
        m_modelScratch.push_back(aabbModel);
    }

    if (m_modelScratch.empty()) return;

    auto& glView = static_cast<GLBackend&>(backend).getView();
    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    shader->bind();
    STATS_RECORD_SHADER_SWITCH();

    using namespace GLConfig::UniformNames;
    // CameraBlock UBO (binding 2) is bound by GLView for the frame.
    shader->setUniform3fv(Color, view.environment.debugColor);

    const uint32_t instanceCount = static_cast<uint32_t>(m_modelScratch.size());
    m_instanceBuffer.update(m_modelScratch.data(), instanceCount);
    m_instanceBuffer.attachToVAO(*m_aabb->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
    m_aabb->drawInstanced(GL_LINES, instanceCount);
}

} // namespace Engine
