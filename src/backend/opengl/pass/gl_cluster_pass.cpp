#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_cluster_pass.h"

#include <GL/glew.h>

#include "gl_compute_shader.h"

#include "gl_frame_context.h"
#include "data/gl_cluster_grid.h"
#include "system/render/render_view.h"

namespace Engine {

GLClusterPass::GLClusterPass()
    : m_compute(std::make_unique<Core::ComputeShader>("shaders/clustering")) {}

GLClusterPass::~GLClusterPass() = default;

void GLClusterPass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;

    // The cluster depth slices run zNear..zFar exponentially.
    const float zNear = view.camera.zNear;
    const float zFar  = view.camera.zFar;

    // Light SSBO (binding 0) is already bound by the backend; bind the grid we
    // write (binding 1).
    ctx.clusters.bind();

    m_compute->bind();
    m_compute->setUniformMatrix4fv("u_view",          view.camera.view);
    m_compute->setUniformMatrix4fv("u_invProjection", view.camera.invProjection);
    m_compute->setUniform2f("u_screenSize",
                            static_cast<float>(view.viewportWidth),
                            static_cast<float>(view.viewportHeight));
    m_compute->setUniform1f("u_zNear", zNear);
    m_compute->setUniform1f("u_zFar",  zFar);

    // One invocation per cluster, 64 per work group.
    const uint32_t groups = (GLClusterGrid::NUM_CLUSTERS + 63u) / 64u;
    m_compute->dispatch(groups);

    // Make the grid writes visible to the forward pass's SSBO reads. (This
    // vkmGL has no Context barrier wrapper, so issue it directly.)
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace Engine
