#define VKM_LOG_CATEGORY "COOK"

#include "cook/mesh_processing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <meshoptimizer.h>
#include <xatlas.h>

#include "logger.h"

namespace Vkm::Engine {

namespace {

// How far a level may move the surface, as a share of the mesh's extent.
constexpr float MAX_LEVEL_DEVIATION = 0.05f;

// gltfpack's normal weighting, under which a crease survives longer than a flat run.
constexpr float NORMAL_WEIGHT = 0.5f;

// What meshoptimizer and the remap assume and a release build does not check.
bool isWellFormed(const MeshAsset& mesh) {
    if (mesh.indices.size() % 3 != 0) return false;
    if (!mesh.skin.empty() && mesh.skin.size() != mesh.vertices.size()) return false;
    if (!mesh.lightmapUVs.empty() && mesh.lightmapUVs.size() != mesh.vertices.size()) return false;
    for (const uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) return false;
    }
    return true;
}

} // namespace

MeshAsset decimateMesh(const MeshAsset& src, float ratio, float* outError) {
    if (outError) *outError = 0.0f;

    // Refused, not unskinned: such a level would render in bind pose while LOD0 animates.
    if (!src.skin.empty()) {
        LOG_WARNING(
            "'%s' is skinned; a level would drop its bindings, so it is not decimated",
            src.name().c_str()
        );
        return {};
    }

    if (src.vertices.empty() || src.indices.size() < 3 || !(ratio > 0.0f && ratio < 1.0f)) {
        return {};
    }
    if (!isWellFormed(src)) {
        LOG_WARNING(
            "'%s' is malformed (a partial triangle or an index past its vertices); it is not decimated",
            src.name().c_str()
        );
        return {};
    }

    const size_t triangles   = src.indices.size() / 3;
    const size_t targetCount = static_cast<size_t>(static_cast<float>(triangles) * ratio) * 3;
    const float  weights[3]  = {NORMAL_WEIGHT, NORMAL_WEIGHT, NORMAL_WEIGHT};

    MeshAsset out;
    out.indices.resize(src.indices.size());
    float relativeError = 0.0f;
    const size_t kept = meshopt_simplifyWithAttributes(
        out.indices.data(),
        src.indices.data(),
        src.indices.size(),
        &src.vertices[0].position.x,
        src.vertices.size(),
        sizeof(Vertex),
        &src.vertices[0].normal.x,
        sizeof(Vertex),
        weights,
        3,
        nullptr,
        targetCount,
        MAX_LEVEL_DEVIATION,
        meshopt_SimplifyPrune,
        &relativeError
    );
    out.indices.resize(kept);

    // Nothing removed, or everything: neither is a coarser version of the mesh.
    if (kept == 0 || kept >= src.indices.size()) return {};

    if (outError) {
        *outError = relativeError * meshopt_simplifyScale(
            &src.vertices[0].position.x,
            src.vertices.size(),
            sizeof(Vertex)
        );
    }

    // Keep only the source vertices the kept triangles use, in order of use, every stream alike.
    std::vector<unsigned int> remap(src.vertices.size());
    const size_t used = meshopt_optimizeVertexFetchRemap(
        remap.data(),
        out.indices.data(),
        out.indices.size(),
        src.vertices.size()
    );
    meshopt_remapIndexBuffer(out.indices.data(), out.indices.data(), out.indices.size(), remap.data());
    out.vertices.resize(src.vertices.size());
    meshopt_remapVertexBuffer(
        out.vertices.data(),
        src.vertices.data(),
        src.vertices.size(),
        sizeof(Vertex),
        remap.data()
    );
    out.vertices.resize(used);
    if (!src.lightmapUVs.empty()) {
        out.lightmapUVs.resize(src.lightmapUVs.size());
        meshopt_remapVertexBuffer(
            out.lightmapUVs.data(),
            src.lightmapUVs.data(),
            src.lightmapUVs.size(),
            sizeof(glm::vec2),
            remap.data()
        );
        out.lightmapUVs.resize(used);
    }

    out.computeAndSetBounds();
    return out;
}

void optimizeMeshForGpu(MeshAsset& mesh) {
    if (mesh.indices.empty() || mesh.vertices.empty()) return;
    if (!isWellFormed(mesh)) {
        LOG_WARNING(
            "'%s' is malformed (a partial triangle, an index past its vertices or a skin stream of "
            "another length); it is baked in import order",
            mesh.name().c_str()
        );
        return;
    }

    const size_t indexCount  = mesh.indices.size();
    const size_t vertexCount = mesh.vertices.size();

    // No overdraw pass: GLDepthPrepass already rejects hidden fragments, so it would only cost
    // vertex-cache hits.
    meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), indexCount, vertexCount);

    // One remap for every per-vertex stream, so the skin moves with its vertex.
    std::vector<unsigned int> remap(vertexCount);
    const size_t used = meshopt_optimizeVertexFetchRemap(
        remap.data(),
        mesh.indices.data(),
        indexCount,
        vertexCount
    );
    meshopt_remapIndexBuffer(mesh.indices.data(), mesh.indices.data(), indexCount, remap.data());
    meshopt_remapVertexBuffer(
        mesh.vertices.data(),
        mesh.vertices.data(),
        vertexCount,
        sizeof(Vertex),
        remap.data()
    );
    mesh.vertices.resize(used);
    if (!mesh.skin.empty()) {
        meshopt_remapVertexBuffer(
            mesh.skin.data(),
            mesh.skin.data(),
            vertexCount,
            sizeof(SkinVertex),
            remap.data()
        );
        mesh.skin.resize(used);
    }
    if (!mesh.lightmapUVs.empty()) {
        meshopt_remapVertexBuffer(
            mesh.lightmapUVs.data(),
            mesh.lightmapUVs.data(),
            vertexCount,
            sizeof(glm::vec2),
            remap.data()
        );
        mesh.lightmapUVs.resize(used);
    }
}

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
    if (!isWellFormed(mesh)) {
        LOG_WARNING("'%s' is malformed; it takes no lightmap coordinates", name.c_str());
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
