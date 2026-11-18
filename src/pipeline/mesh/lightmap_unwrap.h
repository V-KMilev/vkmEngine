#pragma once

#include <cstddef>
#include <string>

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

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

inline constexpr size_t LIGHTMAP_MAX_TRIANGLES = 200000;  ///< The most triangles a mesh is unwrapped with.

} // namespace Vkm::Engine
