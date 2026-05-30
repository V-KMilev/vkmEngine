#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_aabb_debug_pass.h"

#include "logger.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"


namespace Engine {

bool GLAABBDebugPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.aabbDebug.enabled;
}


GLAABBDebugPass::GLAABBDebugPass(ShaderHandle shader)
    : GLRenderPass("GLAABBDebugPass")
    , m_shader(shader)
{
    initialize();
}

void GLAABBDebugPass::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {}

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

void GLAABBDebugPass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;

    if (!view.environment.aabbDebug.enabled) return;

    m_modelScratch.clear();
    m_modelScratch.reserve(view.drawables.size());

    for (const auto& drawable : view.drawables) {
        // VisibilitySystem already transformed the mesh's local AABB into
        // world space and stored it on the drawable - reuse it instead of
        // re-running localToWorldAABB here.
        if (drawable.worldMin == drawable.worldMax) continue;

        const glm::vec3 center = (drawable.worldMin + drawable.worldMax) * 0.5f;
        const glm::vec3 size   = drawable.worldMax - drawable.worldMin;

        glm::mat4 aabbModel = glm::translate(glm::mat4(1.0f), center);
        aabbModel = glm::scale(aabbModel, size);
        m_modelScratch.push_back(aabbModel);
    }

    if (m_modelScratch.empty()) return;

    // Route to the HDR FBO's overlay attachment instead of the HDR colour
    // attachment - diagnostic colours skip the composite's tonemap chain
    // and the visible AABBs show their authored aabbDebug.color exactly.
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    if (!hdr.isReady()) return;
    hdr.bindForOverlay();

    auto& glView = gl.getView();
    GLShader* shader = glView.resolveShader(m_shader, resources);
    if (!shader) return;

    shader->bind();

    using namespace GLConfig::UniformNames;
    // CameraBlock UBO (binding 2) is bound by GLView for the frame.
    shader->setUniform3fv(Color, view.environment.aabbDebug.color);

    const uint32_t instanceCount = static_cast<uint32_t>(m_modelScratch.size());
    m_instanceBuffer.update(m_modelScratch.data(), instanceCount);
    m_instanceBuffer.attachToVAO(*m_aabb->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
    m_aabb->drawInstanced(GL_LINES, instanceCount);
}

} // namespace Engine
