#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

/**
 * @brief Where a terrain's plants stand: each grid square grows as many of a layer's plants as the
 *        density painted at its corners asks, placed, turned and sized by a hash of the square and
 *        the layer's seed.
 *
 * A function of the asset alone, so every end of a networked game and every load grows the same
 * plants, and a stroke moves only those where it painted.
 */
namespace TerrainPlants {

/// The most plants one grid square grows of one layer, whatever its density asks.
inline constexpr uint32_t MAX_PER_SQUARE = 64;

/**
 * @brief One plant: which layer, and where in the terrain it stands.
 */
struct Plant {
    uint32_t  layer = 0;      ///< Index into TerrainAsset::foliage.
    glm::mat4 local{1.0f};    ///< Its place in the terrain: standing on the surface, turned and sized.
};

/**
 * @brief Every plant @p terrain grows.
 *
 * @param terrain The terrain; a layer with no density map grows none.
 * @param[out] out Replaced with them, layer by layer, row by row.
 */
void scatter(const TerrainAsset& terrain, std::vector<Plant>& out);

} // namespace TerrainPlants

} // namespace Vkm::Engine
