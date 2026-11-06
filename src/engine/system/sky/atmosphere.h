#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

struct SkySettings;

} // namespace Vkm::Engine

namespace Vkm::Engine::Atmosphere {

// The procedural sky's atmosphere: an Earth-like planet under Rayleigh, Mie and ozone
// layers, in meters. The one statement of it: the backend writes it into every shader's
// prelude, so sky and sunlight agree.

constexpr float PLANET_RADIUS = 6371000.0f;  ///< Sea level, from the planet's center.
constexpr float TOP_RADIUS    = 6471000.0f;  ///< Where the atmosphere ends.
constexpr float EYE_ALTITUDE  = 1000.0f;     ///< Where the scene stands, above sea level.

inline const glm::vec3 RAYLEIGH_SCATTERING = {5.802e-6f, 13.558e-6f, 33.1e-6f};
    ///< Sea-level Rayleigh scattering per meter (Bruneton's); it absorbs nothing.
constexpr float RAYLEIGH_SCALE_HEIGHT = 8000.0f;

constexpr float MIE_SCATTERING   = 21.0e-6f;  ///< Sea-level, per meter, gray.
constexpr float MIE_EXTINCTION   = 1.1f;      ///< Extinction over scattering.
constexpr float MIE_SCALE_HEIGHT = 1200.0f;

inline const glm::vec3 OZONE_ABSORPTION = {0.650e-6f, 1.881e-6f, 0.085e-6f};
    ///< Ozone's absorption per meter at its peak (Hillaire's). It scatters nothing; absorbing orange, it
    ///< keeps the twilight sky overhead blue.
constexpr float OZONE_ALTITUDE   = 25000.0f;  ///< Where its tent-shaped density peaks.
constexpr float OZONE_HALF_WIDTH = 15000.0f;  ///< Altitude either side where it reaches zero.

constexpr float GROUND_ALBEDO = 0.3f;  ///< The planet's diffuse reflectance, below the horizon and in the
                                       ///< multiple scattering.

constexpr int TRANSMITTANCE_STEPS = 256;  ///< Midpoint samples along a path to the top of the air, on the CPU
                                          ///< and in the GPU's table alike, so the sun's color and the sky's
                                          ///< agree; within 0.1% of converged at every elevation.

constexpr float MIE_G = 0.76f;  ///< The Mie phase's asymmetry: higher gathers the haze tighter round the sun.

/**
 * @brief The air's coefficients under an authored sky, per meter at sea level.
 *
 * The constants above, Mie's scaled by `SkySettings::mie`; integrators apply each
 * layer's scale height. Rayleigh and ozone take no scale.
 */
struct Coefficients {
    glm::vec3 rayleighScattering;  ///< Also its extinction: Rayleigh absorbs nothing
    glm::vec3 mieScattering;
    glm::vec3 mieExtinction;       ///< Scattering plus absorption

    bool operator==(const Coefficients& other) const {
        return rayleighScattering == other.rayleighScattering
            && mieScattering      == other.mieScattering
            && mieExtinction      == other.mieExtinction;
    }
};

/**
 * @brief The atmosphere's coefficients under @p sky.
 *
 * @param sky The procedural sky whose scattering scales apply.
 * @return Its Rayleigh and Mie coefficients.
 */
Coefficients coefficients(const SkySettings& sky);

/**
 * @brief How much sunlight crosses @p air, relative to a sun straight overhead.
 *
 * So an overhead sun keeps its authored color; zero once the planet blocks it.
 *
 * @param air       The scattering air; ozone comes with it.
 * @param sunHeight The sine of the sun's elevation.
 * @return Per-channel transmittance, 1 at the zenith.
 */
glm::vec3 sunTransmittance(const Coefficients& air, float sunHeight);

/**
 * @brief How much sunlight reaches the scene under @p sky.
 *
 * @param sky Supplies the sun's elevation and the haze.
 * @return Per-channel transmittance, 1 at the zenith.
 */
glm::vec3 sunTransmittance(const SkySettings& sky);

/**
 * @brief The sun's illuminance above the air, which lights the sky.
 *
 * What, through the air straight down, arrives as `sky.lightColor` times
 * `sky.lightIntensity`: the one sun the scene's light and the sky both stand for.
 *
 * @param sky Supplies the sun's color and intensity and the air.
 * @return Per-channel illuminance at the top of the atmosphere.
 */
glm::vec3 solarIlluminance(const SkySettings& sky);

/**
 * @brief The sun's one color: the authored daylight color through the atmosphere.
 *
 * @param sky Supplies the sun's color, elevation and the air.
 * @return `sky.lightColor` times sunTransmittance.
 */
glm::vec3 sunlight(const SkySettings& sky);

} // namespace Vkm::Engine::Atmosphere
