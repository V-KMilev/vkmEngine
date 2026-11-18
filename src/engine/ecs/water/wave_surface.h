#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

#include "ecs/settings/wave_settings.h"
#include "ecs/water/wave_spectrum.h"

namespace Vkm::Engine {

class Swell;
class SwellMoment;

/**
 * @brief The water over a world point: its height above the surface's rest, and the surface's normal.
 */
struct WaveSample {
    float     height = 0.0f;
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};
};

/**
 * @brief The waves' surface at one moment: their swell and surges, and what they do to the water.
 *
 * The CPU's statement of the drawn sea: the renderer draws the same swell at the same moment (SwellMoment)
 * and the vertex shaders move each Water's surface by its share of it, faded and surged as here
 * (shaders/opengl/include/waves.glsl), so what floats rides what is drawn. Heights are above the moved
 * surface's rest, and points are on the XZ plane.
 *
 * A point of the water that rests at `rest` is moved by displacement(rest): the swell carries the
 * water toward its crests as well as up. heightAt() and normalAt() answer for a point fixed in
 * the world instead, finding the water that is over it. Each takes the `share` of the waves a
 * body shows (Water::waves), which scales the whole of their movement.
 */
struct WaveSurface {
    static constexpr float SURGE_WHOLE = 0.6f;  ///< Of a surge's length, the share in the middle at its full
                                                ///< height; past it each end tapers away.

    static constexpr uint32_t INVERSE_STEPS = 4;  ///< Fixed-point steps from a world point back to the water
                                                  ///< resting under it: the swell rarely folds
                                                  ///< (Swell::choppiness), so each step contracts, and these
                                                  ///< few leave millimeters.

    std::shared_ptr<const SwellMoment> swell;  ///< The swell at this moment; null for a windless sea or an
                                               ///< empty band.
    std::array<WaveSurge, WaveSettings::MAX_SURGES> surges{};  ///< The waves', as they stand this moment,
                                                               ///< each direction unit; one with none is left
                                                               ///< out.
    glm::vec2 center      = glm::vec2(0.0f);   ///< Where the waves are whole.
    float     calmRadius  = 0.0f;
    float     stillRadius = 0.0f;
    double    time        = 0.0;               ///< The moment: seconds into the sea's clock.

    /**
     * @brief The surface of @p waves, @p time seconds in, its swell made for them: as at() given
     *        Swell::of(waves.shape).
     *
     * @param waves The scene's waves.
     * @param time  Seconds into the waves' clock.
     * @return The surface.
     */
    static WaveSurface at(const WaveSettings& waves, double time);

    /**
     * @brief The surface of @p waves, @p time seconds in.
     *
     * Every end that builds one from the same waves at the same time sees the same water. Cheap
     * to build every frame: the swell is drawn only when the water is asked about.
     *
     * @param swell The waves' swell (Swell::of), made once a change of their shape.
     * @param waves The scene's waves.
     * @param time  Seconds, as NetSession::worldTime gives them; held in double so the loop's phase
     *              stays exact for hours.
     * @return The surface; with no swell for windless or bandless waves, though their surges still
     *         lift the water.
     */
    static WaveSurface at(std::shared_ptr<const Swell> swell, const WaveSettings& waves, double time);

    /// How far through WaveSpectrum::LOOP @p time seconds falls, 0..1.
    static float cycleOf(double time);

    /// How much of the waves reach @p rest: 1 inside calmRadius, 0 from stillRadius out.
    float reach(const glm::vec2& rest) const;

    /// Where the water resting at @p rest has been carried: X and Z across, Y up.
    glm::vec3 displacement(const glm::vec2& rest, float share = 1.0f) const;

    /// The water's height over the world point @p point, above the surface's rest.
    float heightAt(const glm::vec2& point, float share = 1.0f) const;

    /// The surface's unit normal over the world point @p point.
    glm::vec3 normalAt(const glm::vec2& point, float share = 1.0f) const;

    /// Both, for the price of finding the water over @p point once: what floating things ask.
    WaveSample sampleAt(const glm::vec2& point, float share = 1.0f) const;

    /// Spectral significant wave height, 4 sqrt(m0): the mean of the highest third of the waves.
    float significantHeight() const;

    /// The farthest the water can be carried: x across, y up or down, with every wave in step
    /// and every surge crossing there.
    glm::vec2 farthestReach() const;

    /// The same surface without its surges: the wind's sea alone.
    WaveSurface withoutSurges() const;
};

} // namespace Vkm::Engine
