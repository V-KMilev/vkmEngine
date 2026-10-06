#include "system/sky/atmosphere.h"

#include <cmath>

#include "ecs/environment.h"

namespace Vkm::Engine::Atmosphere {

namespace {

// Midpoint samples along the path; every elevation lands within 0.1% of the
// converged transmittance.
constexpr int STEPS = 128;

// Optical depth toward (cos e, sin e): x Rayleigh, y Mie, in density-weighted
// metres. Negative when the planet is in the way.
glm::vec2 opticalDepth(float sinElevation) {
    const double r0   = PLANET_RADIUS + EYE_ALTITUDE;
    const double mu   = sinElevation;
    const double disc = r0 * r0 * (mu * mu - 1.0);

    // Below the horizon the ray meets the ground before it leaves the air.
    if (mu < 0.0 && disc + double(PLANET_RADIUS) * PLANET_RADIUS >= 0.0) return glm::vec2(-1.0f);

    const double length = -r0 * mu + std::sqrt(disc + double(TOP_RADIUS) * TOP_RADIUS);
    const double step   = length / STEPS;
    double rayleigh = 0.0;
    double mie      = 0.0;
    for (int i = 0; i < STEPS; ++i) {
        const double s      = (i + 0.5) * step;
        const double height = std::sqrt(r0 * r0 + s * s + 2.0 * r0 * mu * s) - PLANET_RADIUS;
        rayleigh += std::exp(-height / RAYLEIGH_SCALE_HEIGHT);
        mie      += std::exp(-height / MIE_SCALE_HEIGHT);
    }
    return glm::vec2(static_cast<float>(rayleigh * step), static_cast<float>(mie * step));
}

glm::vec3 extinction(const Coefficients& air, glm::vec2 depth) {
    return air.rayleighScattering * depth.x + air.mieExtinction * depth.y;
}

} // namespace

Coefficients coefficients(const SkySettings& sky) {
    Coefficients air;
    air.rayleighScattering = RAYLEIGH_SCATTERING * sky.rayleigh;
    air.mieScattering      = glm::vec3(MIE_SCATTERING * sky.mie);
    air.mieExtinction      = air.mieScattering * MIE_EXTINCTION;
    return air;
}

glm::vec3 sunTransmittance(const SkySettings& sky) {
    const glm::vec2 toSun = opticalDepth(std::sin(glm::radians(sky.sunElevation)));
    if (toSun.x < 0.0f) return glm::vec3(0.0f);

    // The ratio of transmittances, in the exponent; the zenith depth depends on the planet alone.
    static const glm::vec2 s_zenith = opticalDepth(1.0f);
    const Coefficients air = coefficients(sky);
    return glm::exp(-(extinction(air, toSun) - extinction(air, s_zenith)));
}

glm::vec3 sunlight(const SkySettings& sky) {
    return sky.lightColor * sunTransmittance(sky);
}

} // namespace Vkm::Engine::Atmosphere
