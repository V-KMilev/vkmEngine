#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief A grid of @p asked cells held to its bounds: each axis within @p lowest .. @p highest, and
 *        then, if the cells still number more than @p mostCells, all three scaled down alike.
 *
 * No scaled axis falls below @p lowest, or one, so an axis asked for stays; one lifted back to it
 * is paid for by the others, a cell at a time off the largest. The cap holds whatever is asked
 * when that floor cubed is within @p mostCells; past it, the grid stops at the floor on every axis.
 *
 * @param asked     Cells along X, Y and Z.
 * @param lowest    Fewest along an axis; zero lets an axis be empty.
 * @param highest   Most along an axis.
 * @param mostCells Most cells in all.
 * @return The grid.
 */
inline glm::uvec3 fitGrid(const glm::uvec3& asked, uint32_t lowest, uint32_t highest, double mostCells) {
    glm::uvec3 dims = glm::clamp(asked, glm::uvec3(lowest), glm::uvec3(highest));
    const double count = static_cast<double>(dims.x) * dims.y * dims.z;
    if (count <= mostCells) return dims;

    const uint32_t floorAxis = std::max(lowest, 1u);
    const double   scale     = std::cbrt(mostCells / count);
    for (int axis = 0; axis < 3; ++axis) {
        const auto scaled = static_cast<uint32_t>(std::floor(dims[axis] * scale));
        dims[axis] = std::max(scaled, floorAxis);
    }
    const auto cells = [&] { return static_cast<double>(dims.x) * dims.y * dims.z; };
    while (cells() > mostCells) {
        int widest = 0;
        for (int axis = 1; axis < 3; ++axis) {
            if (dims[axis] > dims[widest]) widest = axis;
        }
        if (dims[widest] <= floorAxis) break;
        --dims[widest];
    }
    return dims;
}

} // namespace Vkm::Engine::Math
