#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

#include "ecs/component/render/ocean.h"
#include "system/ocean/ocean_spectrum.h"
#include "system/ocean/ocean_swell.h"

namespace Vkm::Engine {

/// The water over a world point: its height above the surface's rest, and the surface's normal.
struct OceanSample {
    float     height = 0.0f;
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};
};

/**
 * @brief An ocean's surface at one moment: its swell and surges, and what they do to the water.
 *
 * The CPU's statement of the drawn sea: the renderer draws the same swell at the same moment
 * (SwellMoment) and the vertex shaders move a Sea material's surface by it, faded and surged
 * as here (shaders/ocean.glsl), so what floats rides what is drawn. Heights are above the moved
 * surface's rest, and points are on the XZ plane.
 *
 * A point of the water that rests at `rest` is moved by displacement(rest): the swell carries the
 * water toward its crests as well as up. heightAt() and normalAt() answer for a point fixed in
 * the world instead, finding the water that is over it.
 */
struct OceanSurface {
    /// Of a surge's length, the share in the middle at its full height; past it each end tapers away.
    static constexpr float SURGE_WHOLE = 0.6f;

    /// Fixed-point steps from a world point back to the water resting under it: the swell rarely
    /// folds (OceanSwell::choppiness), so each step contracts; these few leave millimetres.
    static constexpr uint32_t INVERSE_STEPS = 4;

    /// The swell at this moment; null for a windless sea or an empty band.
    std::shared_ptr<const SwellMoment> swell;
    /// The ocean's, as they stand this moment, each direction unit; one with none is left out.
    std::array<OceanSurge, Ocean::MAX_SURGES> surges{};
    glm::vec2 centre      = glm::vec2(0.0f);  ///< Where the waves are whole.
    float     calmRadius  = 0.0f;
    float     stillRadius = 0.0f;
    double    time        = 0.0;  ///< The moment: seconds into the sea's clock.

    /**
     * @brief The surface of @p ocean, centred at @p centre, @p time seconds in, its swell made
     *        for it: as at() given OceanSwell::of(OceanShape::of(ocean)).
     *
     * @param ocean  The authored sea.
     * @param centre Where it stands, world space.
     * @param time   Seconds into the sea's clock.
     * @return The surface.
     */
    static OceanSurface at(const Ocean& ocean, const glm::vec3& centre, double time);

    /**
     * @brief The surface of @p ocean, centred at @p centre, @p time seconds in.
     *
     * Every end that builds one from the same sea at the same time sees the same water. Cheap
     * to build every frame: the swell is drawn only when the water is asked about.
     *
     * @param swell  @p ocean's swell (OceanSwell::of), made once a change of the sea.
     * @param ocean  The authored sea.
     * @param centre Its Transform's world position; only X and Z are read.
     * @param time   Seconds, as NetSession::worldTime gives them; held in double so the loop's
     *               phase stays exact for hours.
     * @return The surface; with no swell for a windless or bandless ocean, though its surges
     *         still lift the water.
     */
    static OceanSurface at(
        std::shared_ptr<const OceanSwell> swell,
        const Ocean& ocean,
        const glm::vec3& centre,
        double time
    );

    /// How far through OceanSpectrum::LOOP @p time seconds falls, 0..1.
    static float cycleOf(double time);

    /// How much of the waves reach @p rest: 1 inside calmRadius, 0 from stillRadius out.
    float reach(const glm::vec2& rest) const;

    /// Where the water resting at @p rest has been carried: X and Z across, Y up.
    glm::vec3 displacement(const glm::vec2& rest) const;

    /// The water's height over the world point @p point, above the surface's rest.
    float heightAt(const glm::vec2& point) const;

    /// The surface's unit normal over the world point @p point.
    glm::vec3 normalAt(const glm::vec2& point) const;

    /// Both, for the price of finding the water over @p point once: what floating things ask.
    OceanSample sampleAt(const glm::vec2& point) const;

    /// Spectral significant wave height, 4 sqrt(m0): the mean of the highest third of the waves.
    float significantHeight() const;

    /// The farthest the water can be carried: x across, y up or down, with every wave in step
    /// and every surge crossing there.
    glm::vec2 farthestReach() const;

    /// The same surface without its surges: the wind's sea alone.
    OceanSurface withoutSurges() const;
};

/**
 * @brief An ocean's ripples at one moment: its spectrum on below the swell's shortest wave, down to
 *        `shortestRipple`.
 *
 * Too short to move a vertex or anything afloat, so only the renderer reads them, for the
 * water's light and foam (rendering.md, "The ocean").
 */
struct OceanRipples {
    OceanSpectrum spectrum;
    /// The band's lowest wavenumber, radians a metre: the swell's shortest wave's.
    float    longest    = 0.0f;
    float    shortest   = 0.0f;  ///< Its highest; no ripples unless above `longest`.
    float    choppiness = 0.0f;  ///< How far a crest squeezes the surface, as the swell's.
    uint32_t seed       = 0;
    float    cycle      = 0.0f;  ///< How far through OceanSpectrum::LOOP, 0..1.

    /**
     * @brief The ripples of @p ocean, @p time seconds in: none for a windless ocean.
     *
     * @param ocean The authored sea.
     * @param time  Seconds, as NetSession::worldTime gives them.
     * @return Its ripples; adding them moves no wave.
     */
    static OceanRipples at(const Ocean& ocean, double time);

    /// Whether there are any: wind, and a band.
    bool any() const { return shortest > longest && spectrum.any(); }
};

} // namespace Vkm::Engine
