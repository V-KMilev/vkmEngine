#pragma once

#include <cstdint>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/terrain_asset.h"
#include "core/frame/terrain_frame.h"

namespace Vkm::Engine {

/**
 * @brief How a terrain is cut into meshes: square chunks, each at levels that take every vertex,
 *        then every second, fourth and eighth, for the cull to pick by distance.
 *
 * Each level hangs a skirt from each rim it shares with another chunk, so where two neighbors meet
 * at different levels the gap between their edges shows the skirt, not the sky. A vertex's
 * lightmap coordinates are its place in the chunk's grid, the same at every level, and a skirt's
 * its rim's. A vertex's uv is the center of its texel of a map with a texel a grid vertex (the
 * layer weights), so a share painted at a vertex is read there.
 */
namespace TerrainChunks {

/**
 * @brief How many chunks @p terrain is cut into along a side.
 *
 * @param terrain A terrain of a valid resolution.
 * @return (resolution - 1) / CELLS.
 */
uint32_t perSide(const TerrainAsset& terrain);

/**
 * @brief Chunk @p cx, @p cz of @p terrain at @p level, in the terrain's local space.
 *
 * Each grid square is two triangles split along its diagonal from (i, j) to (i + 1, j + 1), as
 * TerrainAsset::heightAt reads them; a coarser level's square is the same over its larger span.
 *
 * @param terrain Its grid.
 * @param cx      Along X, below perSide().
 * @param cz      Along Z, below perSide().
 * @param level   Below LEVELS.
 * @return The mesh, its bounds set, its skirts among them.
 */
MeshAsset build(const TerrainAsset& terrain, uint32_t cx, uint32_t cz, uint32_t level);

} // namespace TerrainChunks

} // namespace Vkm::Engine
