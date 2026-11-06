#define VKM_LOG_CATEGORY "COOK"

#include "mesh/lightmap_unwrap.h"

#include <cstdint>
#include <string>
#include <vector>

#include <xatlas.h>

#include "vkm_log/logger.h"

namespace Vkm::Engine {

void unwrapLightmap(MeshAsset& mesh, const std::string& name) {
    if (!mesh.skin.empty() || mesh.vertices.empty() || mesh.indices.size() < 3) return;
    if (mesh.indices.size() / 3 > LIGHTMAP_MAX_TRIANGLES) {
        LOG_WARNING(
            "'%s' has %zu triangles, more than a lightmap unwraps; it is lit live",
            name.c_str(),
            mesh.indices.size() / 3
        );
        return;
    }
    const std::string fault = findMeshFault(mesh);
    if (!fault.empty()) {
        LOG_WARNING("'%s' is malformed (%s); it takes no lightmap coordinates", name.c_str(), fault.c_str());
        return;
    }

    // Its surface, to scale the packing to MeshAsset::LIGHTMAP_PACK_TEXELS whatever the mesh's size.
    double area = 0.0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        area += 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
    }
    if (!(area > 0.0)) return;

    xatlas::Atlas* atlas = xatlas::Create();
    xatlas::MeshDecl decl;
    decl.vertexCount          = static_cast<uint32_t>(mesh.vertices.size());
    decl.vertexPositionData   = &mesh.vertices[0].position;
    decl.vertexPositionStride = sizeof(Vertex);
    decl.vertexNormalData     = &mesh.vertices[0].normal;
    decl.vertexNormalStride   = sizeof(Vertex);
    decl.indexCount           = static_cast<uint32_t>(mesh.indices.size());
    decl.indexData            = mesh.indices.data();
    decl.indexFormat          = xatlas::IndexFormat::UInt32;
    if (xatlas::AddMesh(atlas, decl) != xatlas::AddMeshError::Success) {
        LOG_WARNING("xatlas refused '%s'; it takes no lightmap coordinates", name.c_str());
        xatlas::Destroy(atlas);
        return;
    }

    // Charts filling about half the square, with room for the padding between them.
    xatlas::PackOptions pack;
    pack.padding       = MeshAsset::LIGHTMAP_PACK_PADDING;
    pack.texelsPerUnit = static_cast<float>(MeshAsset::LIGHTMAP_PACK_TEXELS * std::sqrt(0.5 / area));
    xatlas::Generate(atlas, xatlas::ChartOptions{}, pack);

    const xatlas::Mesh& unwrapped = atlas->meshes[0];
    const float side = static_cast<float>(std::max(atlas->width, atlas->height));
    if (atlas->atlasCount != 1 || !(side > 0.0f)) {
        LOG_WARNING("xatlas could not pack '%s'; it takes no lightmap coordinates", name.c_str());
        xatlas::Destroy(atlas);
        return;
    }

    // Square texels: both axes over the longer side.
    std::vector<Vertex>    vertices(unwrapped.vertexCount);
    std::vector<glm::vec2> uvs(unwrapped.vertexCount);
    for (uint32_t i = 0; i < unwrapped.vertexCount; ++i) {
        const xatlas::Vertex& v = unwrapped.vertexArray[i];
        vertices[i] = mesh.vertices[v.xref];
        uvs[i]      = glm::vec2(v.uv[0], v.uv[1]) / side;
    }
    mesh.indices.assign(unwrapped.indexArray, unwrapped.indexArray + unwrapped.indexCount);
    mesh.vertices    = std::move(vertices);
    mesh.lightmapUVs = std::move(uvs);
    xatlas::Destroy(atlas);
}

} // namespace Vkm::Engine
