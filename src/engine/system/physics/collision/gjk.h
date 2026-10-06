#pragma once

#include <glm/glm.hpp>

#include "system/physics/collision/support.h"

namespace Vkm::Engine {

/**
 * @brief Whether two convex shapes overlap, by GJK.
 *
 * Knows nothing of either shape but its support point in a direction.
 *
 * @param a First shape.
 * @param b Second shape.
 * @return True when the two overlap or touch.
 */
bool gjkOverlap(const SupportShape& a, const SupportShape& b);

} // namespace Vkm::Engine
