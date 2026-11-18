#define VKM_LOG_CATEGORY "COOK"

#include "cook/mesh_gpu_order.h"

#include <cstdint>
#include <string>
#include <vector>

#include <meshoptimizer.h>

#include "vkm_log/logger.h"

namespace Vkm::Engine {

void optimizeMeshForGpu(MeshAsset& mesh) {
    if (mesh.indices.empty() || mesh.vertices.empty()) return;
    const std::string fault = findMeshFault(mesh);
    if (!fault.empty()) {
        LOG_WARNING(
            "'%s' is malformed (%s); it is baked in import order",
            mesh.name().c_str(),
            fault.c_str()
        );
        return;
    }

    const size_t indexCount  = mesh.indices.size();
    const size_t vertexCount = mesh.vertices.size();

    // No overdraw pass: the renderer's depth prepass already rejects hidden fragments, so it would only cost
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

} // namespace Vkm::Engine
