#include "storage/gl_cluster_grid.h"

#include <GL/glew.h>

#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"

namespace Vkm::Engine {

GLClusterGrid::GLClusterGrid()  = default;
GLClusterGrid::~GLClusterGrid() = default;

void GLClusterGrid::init() {
    if (m_ssbo) return;
    // GPU-only: allocate uninitialised storage (GLClusterPass writes every
    // cluster's count before it is read each frame).
    m_ssbo = std::make_unique<Vkm::GL::ShaderStorageBuffer>(
        nullptr,
        NUM_CLUSTERS * CLUSTER_STRIDE,
        GL_DYNAMIC_DRAW
    );
}

void GLClusterGrid::bind() const {
    if (m_ssbo) m_ssbo->bindBase(GLBindings::SSBOBindingPoints::CLUSTER_GRID);
}

} // namespace Vkm::Engine
