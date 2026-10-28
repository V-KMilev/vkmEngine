#include "pass/gl_cluster_pass.h"

#include <GL/glew.h>

#include "gl_compute_shader.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "storage/gl_cluster_grid.h"

namespace Vkm::Engine {

GLClusterPass::GLClusterPass()
    : m_compute("shaders/cluster") {}

GLClusterPass::~GLClusterPass() = default;

void GLClusterPass::execute(GLFrameContext& ctx) {
    // Lights and camera are bound by the backend; this binds the grid the dispatch writes.
    ctx.clusters.bind();

    m_compute.bind();

    // One invocation per cluster.
    namespace Groups = GLBindings::ComputeGroups;
    m_compute.dispatch(Groups::covering(GLClusterGrid::NUM_CLUSTERS, Groups::CLUSTERS));

    // Make the grid writes visible to later SSBO reads.
    VKM_GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));
}

} // namespace Vkm::Engine
