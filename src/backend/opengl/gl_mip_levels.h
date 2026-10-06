#pragma once

#include <algorithm>

namespace Vkm::Engine {

/**
 * @brief How many levels a chain halving from @p width x @p height holds.
 *
 * Adds levels while the shorter edge is two texels or more, up to @p maxLevels; one rule, so a
 * small or narrow viewport cuts every backend chain at the same place.
 *
 * @param width     Level 0's width in texels.
 * @param height    Level 0's height in texels.
 * @param maxLevels The most levels the caller will use.
 * @return At least 1.
 */
inline int mipLevelsDownToTwo(int width, int height, int maxLevels) {
    const int shorter = std::max(std::min(width, height), 1);
    int levels = 1;
    while (levels < maxLevels && (shorter >> levels) >= 2) ++levels;
    return levels;
}

} // namespace Vkm::Engine
