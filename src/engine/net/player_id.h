#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief Player slots run from one; zero is "nobody", so a default id is not player one.
 */
using PlayerId = uint16_t;
constexpr PlayerId NO_PLAYER = 0;

} // namespace Vkm::Engine
