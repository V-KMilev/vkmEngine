#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief How far @p seconds are into a cycle of @p period: what a clock-driven motion turns by.
 *
 * Whole cycles are dropped in double before the share is taken, so it stays exact however long
 * the clock has run - BehaviorBase::worldTime's seconds included.
 *
 * @param seconds A clock's seconds.
 * @param period  Seconds a cycle; zero holds at the start.
 * @return 0 to 1.
 */
inline float cycle(double seconds, double period) {
    return period != 0.0 ? static_cast<float>(glm::fract(seconds / period)) : 0.0f;
}

} // namespace Vkm::Engine::Math
