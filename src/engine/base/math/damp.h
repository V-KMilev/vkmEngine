#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

/**
 * @brief The share of the way to close on a target this frame, closing at @p rate a second,
 *        the same whatever the frame rate: `value = mix(value, target, damp(rate, dt))`.
 *
 * @param rate How fast, per second: a tenth of the way left after ln(10) / rate seconds.
 * @param dt   The frame's seconds.
 * @return 0 to 1.
 */
inline float damp(float rate, float dt) { return 1.0f - glm::exp(-rate * dt); }

} // namespace Vkm::Engine::Math
