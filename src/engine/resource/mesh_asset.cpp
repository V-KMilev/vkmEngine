#include "resource/mesh_asset.h"

#include <algorithm>

namespace Engine {

void MeshAsset::computeBounds(const MeshAsset& mesh, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    if (mesh.vertices.empty()) {
        // Empty mesh: set to zero bounds
        boundsMin = glm::vec3(0.0f);
        boundsMax = glm::vec3(0.0f);
        return;
    }

    // Initialize with first vertex
    boundsMin = mesh.vertices[0].position;
    boundsMax = mesh.vertices[0].position;

    // Find min/max across all vertices
    for (const auto& vertex : mesh.vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
}

void MeshAsset::computeAndSetBounds() {
    computeBounds(*this, boundsMin, boundsMax);
}

} // namespace Engine

