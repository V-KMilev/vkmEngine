#include "ecs/water/ripples.h"

#include <algorithm>

#include <glm/gtc/constants.hpp>

#include "ecs/settings/wave_settings.h"
#include "ecs/water/swell.h"
#include "ecs/water/wave_surface.h"

namespace Vkm::Engine {

Ripples Ripples::at(const WaveSettings& waves, double time) {
    Ripples ripples;
    const WaveShape& shape = waves.shape;
    ripples.spectrum = WaveSpectrum::of(shape);
    if (!ripples.spectrum.any()) return ripples;
    ripples.longest    = float(glm::two_pi<double>() / Swell::shortestOf(shape));
    ripples.shortest   = float(glm::two_pi<double>() / double(WaveSettings::SHORTEST_RIPPLE));
    ripples.choppiness = std::clamp(shape.choppiness, 0.0f, 1.0f);
    ripples.seed       = shape.seed;
    ripples.cycle      = WaveSurface::cycleOf(time);
    return ripples;
}

} // namespace Vkm::Engine
