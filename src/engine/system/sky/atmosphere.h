#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

struct SkySettings;

} // namespace Vkm::Engine

namespace Vkm::Engine::Atmosphere {

// The procedural sky's atmosphere: an Earth-like planet under Rayleigh and Mie
// layers, in metres. The one statement of it: the GPU sky bake (`shaders/ibl/sky`,
// geometry via `GLBackend::shaderConstants`) and `sunTransmittance` on the CPU both
// read it, so sky and sunlight agree.

constexpr float PLANET_RADIUS = 6371000.0f;  ///< Sea level, from the planet's centre.
constexpr float TOP_RADIUS    = 6471000.0f;  ///< Where the atmosphere ends.
constexpr float EYE_ALTITUDE  = 1000.0f;     ///< Where the scene stands, above sea level.

/// Sea-level Rayleigh scattering per metre (Bruneton's); it absorbs nothing.
inline const glm::vec3 RAYLEIGH_SCATTERING = {5.802e-6f, 13.558e-6f, 33.1e-6f};
constexpr float RAYLEIGH_SCALE_HEIGHT = 8000.0f;

constexpr float MIE_SCATTERING   = 21.0e-6f;  ///< Sea-level, per metre, grey.
constexpr float MIE_EXTINCTION   = 1.1f;      ///< Extinction over scattering.
constexpr float MIE_SCALE_HEIGHT = 1200.0f;

/**
 * @brief The air's coefficients under an authored sky, per metre at sea level.
 *
 * The constants above scaled by `SkySettings::rayleigh` and `mie`; integrators
 * apply each layer's scale height.
 */
struct Coefficients {
    glm::vec3 rayleighScattering;  ///< Also its extinction: Rayleigh absorbs nothing
    glm::vec3 mieScattering;
    glm::vec3 mieExtinction;       ///< Scattering plus absorption
};

/**
 * @brief The atmosphere's coefficients under @p sky.
 *
 * @param sky The procedural sky whose scattering scales apply.
 * @return Its Rayleigh and Mie coefficients.
 */
Coefficients coefficients(const SkySettings& sky);

/**
 * @brief How much sunlight reaches the scene, relative to a sun straight overhead.
 *
 * So an overhead sun keeps its authored colour; zero once the planet blocks it.
 *
 * @param sky Supplies the sun's elevation and the scattering scales.
 * @return Per-channel transmittance, 1 at the zenith.
 */
glm::vec3 sunTransmittance(const SkySettings& sky);

/**
 * @brief The sun's one colour: the authored daylight colour through the atmosphere.
 *
 * @param sky Supplies the sun's colour, elevation and the air.
 * @return `sky.lightColor` times sunTransmittance.
 */
glm::vec3 sunlight(const SkySettings& sky);

} // namespace Vkm::Engine::Atmosphere
