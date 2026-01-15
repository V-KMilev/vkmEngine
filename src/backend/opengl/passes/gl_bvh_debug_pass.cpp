#include "gl_bvh_debug_pass.h"

#include "logger.h"
#include "print_helper.h"

#include "gl_backend.h"
#include "gl_shader.h"
#include "gl_config.h"

#include "render_view.h"
#include "resource_manager.h"
#include "spatial_index.h"

namespace Engine {

GLBVHDebugPass::GLBVHDebugPass(Core::Shader& shader, const SpatialIndex& spatialIndex)
    : RenderPass("GLBVHDebugPass")
    , m_shader(shader)
    , m_spatialIndex(spatialIndex)
{
    initialize();
}

void GLBVHDebugPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    // Nothing to do
}

void GLBVHDebugPass::initialize() {
    // Create wireframe cube mesh (unit cube from -0.5 to 0.5)
    MeshAsset wireframeMesh;

    wireframeMesh.vertices = {
        { {-0.5f, -0.5f, -0.5f}, {}, {}, {} },
        { { 0.5f, -0.5f, -0.5f}, {}, {}, {} },
        { { 0.5f,  0.5f, -0.5f}, {}, {}, {} },
        { {-0.5f,  0.5f, -0.5f}, {}, {}, {} },
        { {-0.5f, -0.5f,  0.5f}, {}, {}, {} },
        { { 0.5f, -0.5f,  0.5f}, {}, {}, {} },
        { { 0.5f,  0.5f,  0.5f}, {}, {}, {} },
        { {-0.5f,  0.5f,  0.5f}, {}, {}, {} }
    };

    wireframeMesh.indices = {
        0, 1, 1, 5, 5, 4, 4, 0,  // Bottom
        3, 2, 2, 6, 6, 7, 7, 3,  // Top
        0, 3, 1, 2, 5, 6, 4, 7   // Verticals
    };

    m_wireframeCube = std::make_unique<GLMesh>(wireframeMesh);
}

void GLBVHDebugPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) {
    if (!m_enabled) {
        return;
    }

    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLBVHDebugPass requires OpenGL backend");
        return;
    }

    const auto& bvh = m_spatialIndex.getBVH();
    auto nodeBounds = bvh.getNodeBounds(m_maxDepth, m_showLeavesOnly);

    if (nodeBounds.empty()) {
        return;
    }

    m_shader.bind();

    using namespace GLConfig::UniformNames;
    m_shader.setUniformMatrix4fv(ViewProjection, view.camera.viewProjection);

    // Color based on depth (rainbow gradient)
    auto depthToColor = [](int depth) -> glm::vec3 {
        // HSV to RGB with hue based on depth
        float hue = std::fmod(depth * 0.15f, 1.0f);
        float s = 0.8f;
        float v = 0.9f;

        int i = static_cast<int>(hue * 6.0f);
        float f = hue * 6.0f - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - f * s);
        float t = v * (1.0f - (1.0f - f) * s);

        switch (i % 6) {
            case 0: return {v, t, p};
            case 1: return {q, v, p};
            case 2: return {p, v, t};
            case 3: return {p, q, v};
            case 4: return {t, p, v};
            default: return {v, p, q};
        }
    };

    for (const auto& nb : nodeBounds) {
        glm::vec3 center = (nb.min + nb.max) * 0.5f;
        glm::vec3 size = nb.max - nb.min;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, center);
        model = glm::scale(model, size);

        m_shader.setUniformMatrix4fv(Model, model);

        // Leaves are white, internal nodes colored by depth
        glm::vec3 color = nb.isLeaf ? glm::vec3(1.0f) : depthToColor(nb.depth);
        m_shader.setUniform3fv(Color, color);

        m_wireframeCube->draw(GL_LINES);
    }
}

} // namespace Engine

