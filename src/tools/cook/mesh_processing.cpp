#define VKM_LOG_CATEGORY "COOK"

#include "cook/mesh_processing.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <meshoptimizer.h>

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

    // Keep only the source vertices the kept triangles use, in order of use.
    out.vertices.resize(src.vertices.size());
    const size_t used = meshopt_optimizeVertexFetch(
        out.vertices.data(),
        out.indices.data(),
        out.indices.size(),
        src.vertices.data(),
        src.vertices.size(),
        sizeof(Vertex)
    );
    out.vertices.resize(used);

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
}

} // namespace Vkm::Engine
