#pragma once

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief Simplify a mesh to about @p ratio of its triangles, for a coarser LOD level.
 *
 * Normal-weighted quadric edge collapse; seams collapse only along themselves, and kept vertices
 * are @p src's, unmoved, so a level stays within its bounds. Stops short of @p ratio rather than
 * move the surface more than a few percent, so a level may come back finer than asked.
 * A mesh not decimated (skinned, degenerate, bad ratio, nothing removable) comes back empty, not
 * as a copy, which would carry the source's name and replace it.
 *
 * @param src      Source mesh.
 * @param ratio    Share of @p src's triangles to aim for, in (0, 1).
 * @param outError Optional; how far the level departs from @p src in its own units (position and
 *                 weighted normals). Zero when nothing was decimated.
 * @return The decimated MeshAsset (compacted, bounds recomputed, unnamed), or an empty one.
 */
MeshAsset decimateMesh(const MeshAsset& src, float ratio, float* outError = nullptr);

/**
 * @brief Reorder a mesh's triangles and vertices into the order a GPU draws fastest.
 *
 * Vertex cache order, then fetch order; the skin stream moves with the vertices. Triangles,
 * winding and vertex contents are unchanged; an unused vertex is dropped; bounds still hold.
 * A malformed mesh is left untouched and logged.
 *
 * @param mesh Mesh reordered in place.
 */
void optimizeMeshForGpu(MeshAsset& mesh);

} // namespace Vkm::Engine
