#pragma once

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief Reorder a mesh's triangles and vertices into the order a GPU draws fastest.
 *
 * Vertex cache order, then fetch order; the skin and lightmap streams move with the vertices.
 * Triangles, winding and vertex contents are unchanged; an unused vertex is dropped; bounds still hold.
 * A malformed mesh is left untouched and logged. Only the cook runs it, so the same mesh from its
 * recipe and from its cooked file holds its triangles in different orders.
 *
 * @param mesh Mesh reordered in place.
 */
void optimizeMeshForGpu(MeshAsset& mesh);

} // namespace Vkm::Engine
