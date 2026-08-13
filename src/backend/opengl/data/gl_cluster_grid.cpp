#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_cluster_grid.h"

#include <GL/glew.h>

#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"

namespace Engine {

GLClusterGrid::GLClusterGrid()  = default;
GLClusterGrid::~GLClusterGrid() = default;

void GLClusterGrid::init() {
    if (m_ssbo) return;
    // GPU-only: allocate uninitialised storage (the compute pass writes every
    // cluster's count before the forward pass reads it each frame).
    m_ssbo = std::make_unique<Core::ShaderStorageBuffer>(
        nullptr, NUM_CLUSTERS * CLUSTER_STRIDE, GL_DYNAMIC_DRAW);
}

void GLClusterGrid::bind() const {
    if (m_ssbo) m_ssbo->bindBase(GLBindings::SSBOBindingPoints::ClusterGrid);
}

} // namespace Engine
