#pragma once

#include "ecs/component/light.h"

namespace Vkm::Engine {

/**
 * @brief Generate a light component of the given type.
 *
 * Only the fields whose sensible value depends on the type differ from the
 * Light struct's own defaults: how far the light reaches, and the cone angles,
 * which are cleared on everything but a spot.
 *
 * @param type Which kind of light to build.
 * @return A Light component configured for @p type.
 */
Light generateLight(LightType type);

} // namespace Vkm::Engine
