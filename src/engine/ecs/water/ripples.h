#pragma once

#include <cstdint>

#include "ecs/water/wave_spectrum.h"

namespace Vkm::Engine {

struct WaveSettings;

/**
 * @brief The waves' ripples at one moment: their spectrum on below the swell's shortest wave, down to
 *        `WaveSettings::SHORTEST_RIPPLE`.
 *
 * Too short to move a vertex or anything afloat, so only the renderer reads them, for the
 * water's light and foam (water.md, "Waves").
 */
struct Ripples {
    WaveSpectrum spectrum;
    float        longest    = 0.0f;  ///< The band's lowest wavenumber, radians a meter: the swell's shortest
                                     ///< wave's.
    float        shortest   = 0.0f;  ///< Its highest; no ripples unless above `longest`.
    float        choppiness = 0.0f;  ///< How far a crest squeezes the surface, as the swell's.
    uint32_t     seed       = 0;
    float        cycle      = 0.0f;  ///< How far through WaveSpectrum::LOOP, 0..1.

    /**
     * @brief The ripples of @p waves, @p time seconds in: none for windless waves.
     *
     * @param waves The scene's waves.
     * @param time  Seconds, as NetSession::worldTime gives them.
     * @return Their ripples; adding them moves no wave.
     */
    static Ripples at(const WaveSettings& waves, double time);

    /// Whether there are any: wind, and a band.
    bool any() const { return shortest > longest && spectrum.any(); }
};

} // namespace Vkm::Engine
