#include "resource/asset/mesh_asset.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "resource/asset/skeleton_asset.h"

namespace Vkm::Engine {

void MeshAsset::computeAndSetBounds() {
    if (vertices.empty()) {
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
        return;
    }

    boundsMin = vertices[0].position;
    boundsMax = vertices[0].position;
    for (const Vertex& vertex : vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
}

void MeshAsset::computeAndSetSkinRadius(const SkeletonAsset& rig) {
    skinRadius = 0.0f;
    // Empty is unskinned; any other size mismatch breaks the stream's invariant.
    if (skin.empty() || skin.size() != vertices.size()) return;

    // A bone's bind-pose origin: the translation of its inverse bind's inverse.
    std::vector<glm::vec3> origins(rig.inverseBind.size());
    for (size_t bone = 0; bone < origins.size(); ++bone) {
        origins[bone] = glm::vec3(glm::inverse(rig.inverseBind[bone])[3]);
    }

    for (size_t v = 0; v < skin.size(); ++v) {
        const glm::vec3& position = vertices[v].position;
        for (int k = 0; k < 4; ++k) {
            // The rig-root fallback counts too: it moves the vertex like any other.
            if (skin[v].weights[k] == 0) continue;
            const uint16_t bone = skin[v].bones[k];
            if (bone >= origins.size()) continue;
            skinRadius = std::max(skinRadius, glm::distance(position, origins[bone]));
        }
    }
}

std::string findMeshFault(const MeshAsset& mesh) {
    const size_t vertices = mesh.vertices.size();
    if (mesh.indices.size() % 3 != 0) {
        return std::to_string(mesh.indices.size()) + " indices are not whole triangles";
    }
    // The vertex stage reads every stream by one index, so a short one is read past its end.
    if ((!mesh.skin.empty() && mesh.skin.size() != vertices)
        || (!mesh.lightmapUVs.empty() && mesh.lightmapUVs.size() != vertices)) {
        return std::to_string(mesh.skin.size()) + " skin entries and "
            + std::to_string(mesh.lightmapUVs.size()) + " lightmap coordinates are against "
            + std::to_string(vertices) + " vertices";
    }
    for (const uint32_t index : mesh.indices) {
        if (index >= vertices) {
            return "index " + std::to_string(index) + " is past its " + std::to_string(vertices)
                + " vertices";
        }
    }
    // A bone index addresses the pose palette, which the vertex stage reads unclamped; it takes
    // each weight over 255 with no renormalize.
    for (const SkinVertex& skin : mesh.skin) {
        for (const uint16_t bone : skin.bones) {
            if (bone >= SkeletonAsset::MAX_BONES) {
                return "bone index " + std::to_string(bone) + " is past the "
                    + std::to_string(SkeletonAsset::MAX_BONES) + " a rig may have";
            }
        }
        const int sum = skin.weights[0] + skin.weights[1] + skin.weights[2] + skin.weights[3];
        if (sum != 255) return "a vertex's skin weights sum to " + std::to_string(sum) + ", not 255";
    }
    // The cull pads the bones' box by it: one that is not a length makes the box wrong.
    if (!std::isfinite(mesh.skinRadius) || mesh.skinRadius < 0.0f) {
        return "its skin radius " + std::to_string(mesh.skinRadius) + " is not a length";
    }
    return {};
}


} // namespace Vkm::Engine
