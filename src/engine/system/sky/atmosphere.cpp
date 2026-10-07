#include "system/sky/atmosphere.h"

#include <algorithm>
#include <cmath>

#include "ecs/environment.h"

namespace Vkm::Engine::Atmosphere {

namespace {

// Optical depth toward (cos e, sin e): x Rayleigh, y Mie, z ozone, in density-weighted
// metres. Negative when the planet is in the way.
glm::vec3 opticalDepth(float sinElevation) {
    const double r0   = PLANET_RADIUS + EYE_ALTITUDE;
    const double mu   = sinElevation;
    const double disc = r0 * r0 * (mu * mu - 1.0);

    // Below the horizon the ray meets the ground before it leaves the air.
    if (mu < 0.0 && disc + double(PLANET_RADIUS) * PLANET_RADIUS >= 0.0) return glm::vec3(-1.0f);

    const double length = -r0 * mu + std::sqrt(disc + double(TOP_RADIUS) * TOP_RADIUS);
    const double step   = length / TRANSMITTANCE_STEPS;
    double rayleigh = 0.0;
    double mie      = 0.0;
    double ozone    = 0.0;
    for (int i = 0; i < TRANSMITTANCE_STEPS; ++i) {
        const double s      = (i + 0.5) * step;
        const double height = std::sqrt(r0 * r0 + s * s + 2.0 * r0 * mu * s) - PLANET_RADIUS;
        rayleigh += std::exp(-height / RAYLEIGH_SCALE_HEIGHT);
        mie      += std::exp(-height / MIE_SCALE_HEIGHT);
        ozone    += std::max(1.0 - std::abs(height - OZONE_ALTITUDE) / OZONE_HALF_WIDTH, 0.0);
    }
    return glm::vec3(rayleigh * step, mie * step, ozone * step);
}

// The optical depth straight up, which depends on the planet alone, so it is computed once.
const glm::vec3& zenithDepth() {
    static const glm::vec3 s_zenith = opticalDepth(1.0f);
    return s_zenith;
}

glm::vec3 extinction(const Coefficients& air, glm::vec3 depth) {
    return air.rayleighScattering * depth.x + air.mieExtinction * depth.y + OZONE_ABSORPTION * depth.z;
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
    const glm::vec3 toSun = opticalDepth(std::sin(glm::radians(sky.sunElevation)));
    if (toSun.x < 0.0f) return glm::vec3(0.0f);

    // The ratio of transmittances, in the exponent.
    const Coefficients air = coefficients(sky);
    return glm::exp(-(extinction(air, toSun) - extinction(air, zenithDepth())));
}

glm::vec3 solarIlluminance(const SkySettings& sky) {
    return sky.lightIntensity * sky.lightColor * glm::exp(extinction(coefficients(sky), zenithDepth()));
}

glm::vec3 sunlight(const SkySettings& sky) {
    return sky.lightColor * sunTransmittance(sky);
}

} // namespace Vkm::Engine::Atmosphere
