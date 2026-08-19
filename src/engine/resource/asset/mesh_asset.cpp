#include "resource/asset/mesh_asset.h"

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

} // namespace Vkm::Engine
