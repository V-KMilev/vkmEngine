#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief The four SH-L1 coefficient grids of one baked irradiance volume.
 *
 * Laid out as GLIrradianceVolume holds them: one array per coefficient, `x * y * z`
 * cells, X fastest. `sh[0][cell].w` is the verdict on the way in: 1 trusted, 0 for
 * a probe found inside geometry. The other alphas are padding.
 */
using ProbeGridSH = std::array<std::vector<glm::vec4>, 4>;

/**
 * @brief Whether the projection trusted a probe, read from its first coefficient.
 *
 * @param sh0 The probe's cell in `sh[0]`, before any dilation.
 * @return True for a verdict of 1; a half survives rounding in the transfer.
 */
inline bool probeTrusted(const glm::vec4& sh0) {
    return sh0.w >= 0.5f;
}

/**
 * @brief Replace every refused probe with a blend of the trusted probes around it.
 *
 * A probe inside a wall captures the far room, and hardware trilinear filtering
 * would leak that light through; the lookup cannot skip a probe, so the grid is
 * repaired before upload. Refused cells fill from their six axis neighbours, one
 * cell of spread per round, sourcing only the previous round's trusted set so
 * visit order does not matter. Filled cells get alpha 1.
 *
 * @param sh The four grids, repaired in place; untouched unless each holds `x * y * z` cells.
 * @param x  Probe count along X.
 * @param y  Probe count along Y.
 * @param z  Probe count along Z.
 * @return   How many probes were refused. Equal to the cell count when nothing
 *           was trusted and the grid is unrepaired: a caller should refuse the volume.
 */
uint32_t dilateProbeGrid(ProbeGridSH& sh, uint32_t x, uint32_t y, uint32_t z);

} // namespace Vkm::Engine
