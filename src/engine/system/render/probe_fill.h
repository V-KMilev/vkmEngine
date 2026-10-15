#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief How one refused probe is repaired: a blend of trusted probes.
 */
struct ProbeFill {
    uint32_t cell;  ///< The refused probe, X fastest.

    /// The trusted probes it blends, X fastest, and their weights, which sum to one.
    std::vector<std::pair<uint32_t, float>> sources;
};

/**
 * @brief Find how every refused probe is repaired from the trusted probes around it.
 *
 * A probe inside a wall captures the far room, and hardware trilinear filtering would leak that
 * light through; the lookup cannot skip a probe, so the grid is repaired before anything samples
 * it. Refused cells fill from their six axis neighbours, one cell of spread per round, sourcing
 * only cells reached by the previous round, so visit order does not matter. Each fill is a fixed
 * blend of trusted probes, depending on which were trusted and not on what they hold, so a grid
 * judged once is repaired the same way whatever light the backend's bake gives it.
 *
 * @param trusted The verdict per cell, X fastest; anything but `x * y * z` cells finds nothing.
 * @param x       Probe count along X.
 * @param y       Probe count along Y.
 * @param z       Probe count along Z.
 * @return        A fill per refused probe, in cell order; none when nothing was trusted.
 */
std::vector<ProbeFill> probeFillWeights(const std::vector<bool>& trusted, uint32_t x, uint32_t y, uint32_t z);

} // namespace Vkm::Engine
