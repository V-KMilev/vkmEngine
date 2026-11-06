#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief A read-only view of a value per light of the frame, by its place in the list, -1 for none:
 *        its shadow slot (GLShadowPlan) or its lightmap mask channel (GLLightmaps::channels).
 *
 * Two members rather than std::span because the engine is C++17.
 */
struct LightSlots {
    const int* slots = nullptr;
    uint32_t   count = 0;

    /// @return The value of the light at @p index, or -1 when it has none.
    int operator[](uint32_t index) const { return index < count ? slots[index] : -1; }
};

} // namespace Vkm::Engine
