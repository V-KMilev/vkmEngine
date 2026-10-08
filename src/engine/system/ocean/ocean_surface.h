#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "ecs/component/render/ocean.h"

namespace Vkm::Engine {

/// JONSWAP's peak enhancement, and its widths below and above the peak (Hasselmann et al. 1973).
constexpr double JONSWAP_GAMMA       = 3.3;
constexpr double JONSWAP_SIGMA_BELOW = 0.07;
constexpr double JONSWAP_SIGMA_ABOVE = 0.09;

/// One wave of an ocean's sum, at the surface's moment.
struct GerstnerWave {
    glm::vec2 direction = {0.0f, 1.0f};  ///< Unit, on the XZ plane: the way it rolls.
    float     wavenumber = 0.0f;         ///< Radians a metre: 2 pi over its length.
    float     frequency  = 0.0f;         ///< Radians a second, by the dispersion at the ocean's depth.
    float     amplitude  = 0.0f;         ///< Metres.
    /// How far a crest draws the water toward it, in amplitudes; the sum never folds over.
    float     steepness  = 0.0f;
    float     phase      = 0.0f;  ///< Radians at the rest point's origin and this moment, in [0, 2 pi).
};

/// The water over a world point: its height above the surface's rest, and the surface's normal.
struct OceanSample {
    float     height = 0.0f;
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};
};

/**
 * @brief An ocean's surface at one moment: the waves the spectrum gives, and what they do.
 *
 * The one statement of the sum. The renderer uploads these waves and the vertex shaders move a
 * `waves` material's surface by them (shaders/ocean.glsl, the same arithmetic); gameplay asks
 * the same surface where the water is, so what floats rides what is drawn. Heights are above
 * the moved surface's rest, and points are on the XZ plane.
 *
 * A point of the water that rests at `rest` is moved by displacement(rest): Gerstner waves carry
 * the water toward their crests as well as up. heightAt() and normalAt() answer for a point
 * fixed in the world instead, finding the water that is over it.
 */
struct OceanSurface {
    /// Metres a second squared: what pulls every wave back, and sets how fast each rolls.
    static constexpr double GRAVITY = 9.81;

    /// Of a surge's length, the share in the middle at its full height; past it each end tapers away.
    static constexpr float SURGE_WHOLE = 0.6f;

    /// Fixed-point steps from a world point back to the water resting under it: the sum never
    /// folds (its steepness), so each step is a contraction; these few leave millimetres at the steepest.
    static constexpr uint32_t INVERSE_STEPS = 4;

    std::array<GerstnerWave, Ocean::MAX_WAVES> waves{};
    uint32_t  count       = 0;
    /// The ocean's, as they stand this moment, each direction unit; one with none is left out.
    std::array<OceanSurge, Ocean::MAX_SURGES> surges{};
    glm::vec2 centre      = glm::vec2(0.0f);  ///< Where the waves are whole.
    float     calmRadius  = 0.0f;
    float     stillRadius = 0.0f;

    /**
     * @brief The surface of @p ocean, centred at @p centre, @p time seconds in.
     *
     * The waves depend only on the ocean's fields, so every end that builds one at the same time
     * sees the same sea. Cheap enough to build every frame.
     *
     * @param ocean  The authored sea.
     * @param centre Its Transform's world position; only X and Z are read.
     * @param time   Seconds, as NetSession::worldTime gives them; held in double so phases stay
     *               exact for hours.
     * @return The surface; with no waves (count 0) for a windless or bandless ocean, though
     *         its surges still lift the water.
     */
    static OceanSurface at(const Ocean& ocean, const glm::vec3& centre, double time);

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
 * @brief An ocean's ripples at one moment: its spectrum on below `minWavelength`, down to
 * `shortestRipple`, as a density over wavenumber.
 *
 * Too short to move a vertex or anything afloat, so only the renderer reads them, for the
 * water's light and foam (rendering.md, "The ocean"). density() is stated again in
 * shaders/ocean_ripples.glsl; the renderer's tests hold the two to each other.
 */
struct OceanRipples {
    /// Seconds before the ripples repeat: each rolls a whole number of times a loop, so the
    /// GPU's phases stay exact in float however long the sea has run (Tessendorf's looping).
    static constexpr double LOOP = 256.0;

    float     longest     = 0.0f;  ///< The band's lowest wavenumber, radians a metre: the shortest wave's.
    float     shortest    = 0.0f;  ///< Its highest; no ripples unless above `longest`.
    float     peak        = 0.0f;  ///< JONSWAP's peak frequency, radians a second.
    float     alpha       = 0.0f;  ///< Its scale, Phillips' alpha times g squared.
    float     depth       = 0.0f;  ///< Metres; 0 deep.
    float     heightScale = 1.0f;
    float     choppiness  = 0.0f;  ///< How far a crest squeezes the surface, as the waves'.
    glm::vec2 wind        = {0.0f, 1.0f};  ///< Unit, on the XZ plane: the way the waves roll.
    float     spreadPower = 1.0f;  ///< s, in the spread's cos^2s of half the angle off the wind.
    float     spreadScale = 0.0f;  ///< What makes the spread one round the circle.
    uint32_t  seed        = 0;
    float     cycle       = 0.0f;  ///< How far through LOOP, 0..1.

    /**
     * @brief The ripples of @p ocean, @p time seconds in: none for a windless ocean.
     *
     * @param ocean The authored sea.
     * @param time  Seconds, as NetSession::worldTime gives them.
     * @return Its ripples; adding them moves no wave.
     */
    static OceanRipples at(const Ocean& ocean, double time);

    /// Whether there are any: wind, and a band.
    bool any() const { return shortest > longest && alpha > 0.0f; }

    /// Height variance a square radian a metre of wavenumber @p k holds: what the grids draw.
    float density(const glm::vec2& k) const;

    /// The variance of the slope the ripples between wavenumbers @p from and @p to raise.
    float slopeVariance(float from, float to) const;
};

} // namespace Vkm::Engine
