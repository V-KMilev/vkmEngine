#pragma once

#include <cstddef>
#include <string>

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief Simplify a mesh to about @p ratio of its triangles, for a coarser LOD level.
 *
 * Normal-weighted quadric edge collapse; seams collapse only along themselves, and kept vertices
 * are @p src's, unmoved, lightmap coordinates and all, so a level stays within its bounds and
 * reads its base's lightmap. Stops short of @p ratio rather than move the surface more than a few
 * percent, so a level may come back finer than asked.
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
 * @brief Unwrap a mesh into lightmap coordinates (MeshAsset::lightmapUVs), with xatlas.
 *
 * Its charts are packed for a lightmap of about MeshAsset::LIGHTMAP_PACK_TEXELS a side,
 * MeshAsset::LIGHTMAP_PACK_PADDING texels apart, so one given a quarter of that or more still keeps
 * a texel between charts. A seam splits a vertex in two; every stream is carried to both. A skinned
 * mesh is left as it was. So, logged, is one xatlas refuses, and one of more than
 * LIGHTMAP_MAX_TRIANGLES - a scan or a sculpt, too slow for a cook, which is lit live.
 *
 * @param mesh Mesh unwrapped in place.
 * @param name What the log calls it: a mesh still being built has no name of its own.
 */
void unwrapLightmap(MeshAsset& mesh, const std::string& name);

/// The most triangles a mesh is unwrapped with.
inline constexpr size_t LIGHTMAP_MAX_TRIANGLES = 200000;

/**
 * @brief Reorder a mesh's triangles and vertices into the order a GPU draws fastest.
 *
 * Vertex cache order, then fetch order; the skin and lightmap streams move with the vertices.
 * Triangles, winding and vertex contents are unchanged; an unused vertex is dropped; bounds still hold.
 * A malformed mesh is left untouched and logged.
 *
 * @param mesh Mesh reordered in place.
 */
void optimizeMeshForGpu(MeshAsset& mesh);

} // namespace Vkm::Engine
