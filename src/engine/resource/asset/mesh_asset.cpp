#include "resource/asset/mesh_asset.h"

#include <algorithm>
#include <cstddef>

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

} // namespace Vkm::Engine
