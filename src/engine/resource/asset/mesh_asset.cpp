#include "resource/asset/mesh_asset.h"

#include <algorithm>

namespace Engine {

void MeshAsset::computeBounds(const MeshAsset& mesh, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    if (mesh.vertices.empty()) {
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
        return;
    }

    boundsMin = mesh.vertices[0].position;
    boundsMax = mesh.vertices[0].position;

    for (const auto& vertex : mesh.vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
}

void MeshAsset::computeAndSetBounds() {
    computeBounds(*this, boundsMin, boundsMax);
}

} // namespace Engine

