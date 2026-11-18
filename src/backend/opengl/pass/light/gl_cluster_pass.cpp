#include "pass/light/gl_cluster_pass.h"

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/compute_shader.h"

#include "pass/gl_frame_context.h"
#include "convention/gl_bindings.h"
#include "storage/light/gl_cluster_grid.h"

namespace Vkm::Engine {

GLClusterPass::GLClusterPass()
    : m_compute("shaders/opengl/light/cluster") {}

GLClusterPass::~GLClusterPass() = default;

void GLClusterPass::execute(GLFrameContext& ctx) {
    // Lights and camera are bound by the caller; this binds the grid the dispatch writes.
    ctx.clusters.bind();

    m_compute.bind();

    // One invocation per cluster.
    namespace Groups = GLBindings::ComputeGroups;
    m_compute.dispatch(Groups::covering(GLClusterGrid::NUM_CLUSTERS, Groups::CLUSTERS));

    // Make the grid writes visible to later SSBO reads.
    Vkm::OpenGL::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace Vkm::Engine
