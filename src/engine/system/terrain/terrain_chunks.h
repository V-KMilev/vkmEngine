#pragma once

#include <cstdint>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

/**
 * @brief How a terrain is cut into meshes: square chunks, each at levels that take every vertex,
 *        then every second, fourth and eighth, for the cull to pick by distance.
 *
 * Each level hangs a skirt from each rim it shares with another chunk, so where two neighbours meet
 * at different levels the gap between their edges shows the skirt, not the sky. A vertex's uv is
 * the centre of its texel of a map with a texel a grid vertex (the layer weights), so a share
 * painted at a vertex is read there.
 */
namespace TerrainChunks {

/// Grid squares a chunk spans a side at its finest level.
inline constexpr uint32_t CELLS = 32;
/// Its levels, the k-th taking every 2^k-th vertex.
inline constexpr uint32_t LEVELS = 4;

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

/**
 * @brief The level a chunk takes at @p distance from the eye.
 *
 * Each level holds twice as far as the one before: the finest within REACH chunk widths.
 *
 * @param distance   Metres from the eye to the chunk's box.
 * @param chunkWidth Metres a chunk spans.
 * @return The level, below LEVELS.
 */
uint32_t levelAt(float distance, float chunkWidth);

/// Chunk widths from the eye within which a chunk takes its finest level.
inline constexpr float REACH = 1.5f;

} // namespace TerrainChunks

} // namespace Vkm::Engine
