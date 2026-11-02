#define VKM_LOG_CATEGORY "COOK"

#include "mesh/mesh_decimate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <meshoptimizer.h>

#include "vkm_log/logger.h"

namespace Vkm::Engine {

namespace {

// How far a level may move the surface, as a share of the mesh's extent.
constexpr float MAX_LEVEL_DEVIATION = 0.05f;

// gltfpack's normal weighting, under which a crease survives longer than a flat run.
constexpr float NORMAL_WEIGHT = 0.5f;

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
    const std::string fault = findMeshFault(src);
    if (!fault.empty()) {
        LOG_WARNING("'%s' is malformed (%s); it is not decimated", src.name().c_str(), fault.c_str());
        return {};
    }

    const size_t triangles   = src.indices.size() / 3;
    const size_t targetCount = static_cast<size_t>(static_cast<float>(triangles) * ratio) * 3;
    const float  weights[3]  = {NORMAL_WEIGHT, NORMAL_WEIGHT, NORMAL_WEIGHT};

    MeshAsset out;
    out.indices.resize(src.indices.size());
    float relativeError = 0.0f;
    const size_t kept   = meshopt_simplifyWithAttributes(
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

} // namespace Vkm::Engine
