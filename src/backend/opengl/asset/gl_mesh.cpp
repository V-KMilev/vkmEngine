#include "asset/gl_mesh.h"

namespace Vkm::Engine {

namespace {
// The last upload id handed out. Meshes upload on the render thread alone.
uint64_t g_lastUpload = 0;
} // namespace

GLMesh::GLMesh(GLMeshPool& pool, const MeshAsset& mesh)
    : m_pool(pool)
    , m_range(pool.add(mesh))
    , m_uploadId(++g_lastUpload) {}

GLMesh::~GLMesh() {
    m_pool.remove(m_range);
}

void GLMesh::update(const MeshAsset& mesh) {
    m_pool.remove(m_range);
    m_range    = m_pool.add(mesh);
    m_uploadId = ++g_lastUpload;
}

} // namespace Vkm::Engine
