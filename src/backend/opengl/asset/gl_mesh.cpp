#include "asset/gl_mesh.h"

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

namespace {

// The last upload id handed out. Meshes upload on the render thread alone.
uint64_t g_lastUpload = 0;

// The surface of @p mesh its lightmap holds, in its own units, seen along X, Y and Z, when it
// carries lightmap coordinates and the pool took it (@p range); zero otherwise. A triangle with no
// area there, as a terrain's skirt, takes none.
glm::vec3 lightmapAreaOf(const MeshAsset& mesh, const MeshRange& range) {
    if (range.indexCount == 0 || mesh.lightmapUVs.empty()) return glm::vec3(0.0f);
    glm::dvec3 area(0.0);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t ia = mesh.indices[i];
        const uint32_t ib = mesh.indices[i + 1];
        const uint32_t ic = mesh.indices[i + 2];
        const glm::vec2 du = mesh.lightmapUVs[ib] - mesh.lightmapUVs[ia];
        const glm::vec2 dv = mesh.lightmapUVs[ic] - mesh.lightmapUVs[ia];
        if (du.x * dv.y - du.y * dv.x == 0.0f) continue;
        const glm::vec3& a = mesh.vertices[ia].position;
        const glm::vec3  e = mesh.vertices[ib].position - a;
        const glm::vec3  f = mesh.vertices[ic].position - a;
        area += 0.5 * glm::abs(glm::dvec3(glm::cross(e, f)));
    }
    return glm::vec3(area);
}

} // namespace

GLMesh::GLMesh(GLMeshPool& pool, const MeshAsset& mesh)
    : m_pool(pool)
    , m_range(pool.add(mesh))
    , m_uploadId(++g_lastUpload)
    , m_lightmapArea(lightmapAreaOf(mesh, m_range)) {}

GLMesh::~GLMesh() {
    m_pool.remove(m_range);
}

void GLMesh::update(const MeshAsset& mesh) {
    m_pool.remove(m_range);
    m_range        = m_pool.add(mesh);
    m_uploadId     = ++g_lastUpload;
    m_lightmapArea = lightmapAreaOf(mesh, m_range);
}

} // namespace Vkm::Engine
