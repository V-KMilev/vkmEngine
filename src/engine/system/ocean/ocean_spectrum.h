#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

struct Ocean;

/// JONSWAP's peak enhancement, and its widths below and above the peak (Hasselmann et al. 1973).
constexpr double JONSWAP_GAMMA       = 3.3;
constexpr double JONSWAP_SIGMA_BELOW = 0.07;
constexpr double JONSWAP_SIGMA_ABOVE = 0.09;

/**
 * @brief What of an Ocean shapes its waves: every field its spectrum (OceanSpectrum::of) and swell
 *        (OceanSwell::of) are made from, and no other, so oceans of one shape have one swell.
 */
struct OceanShape {
    float    windSpeed     = 0.0f;
    float    windDirection = 0.0f;
    float    fetch         = 0.0f;
    float    depth         = 0.0f;
    float    spread        = 0.0f;
    float    choppiness    = 0.0f;
    float    heightScale   = 0.0f;
    float    minWavelength = 0.0f;
    float    maxWavelength = 0.0f;
    uint32_t seed          = 0;

    /// @p ocean's shape.
    static OceanShape of(const Ocean& ocean);

    bool operator==(const OceanShape& other) const;
    bool operator!=(const OceanShape& other) const { return !(*this == other); }
};

/**
 * @brief An ocean's wave spectrum over wavenumber: what its swell (OceanSwell) and ripples
 *        (OceanRipples) draw their waves from.
 *
 * JONSWAP for the wind and fetch, shortened by TMA's correction where the water is shallow
 * (Horvath 2015), spread round the wind as cos^2s of half the angle off it (Longuet-Higgins).
 * density() is stated again in shaders/ocean_ripples.glsl; the renderer's tests hold the two to
 * each other.
 */
struct OceanSpectrum {
    /// Metres a second squared: what pulls every wave back, and sets how fast each rolls.
    static constexpr double GRAVITY = 9.81;

    /// Seconds before the waves repeat: each rolls a whole number of times a loop, so a GPU's
    /// phases stay exact in float however long the sea has run (Tessendorf's looping).
    static constexpr double LOOP = 256.0;

    float     peak        = 0.0f;  ///< JONSWAP's peak frequency, radians a second.
    float     alpha       = 0.0f;  ///< Its scale, Phillips' alpha times g squared; 0 for no wind.
    float     depth       = 0.0f;  ///< Metres; 0 deep.
    float     heightScale = 1.0f;
    glm::vec2 wind        = {0.0f, 1.0f};  ///< Unit, on the XZ plane: the way the waves roll.
    float     spreadPower = 1.0f;  ///< s, in the spread's cos^2s of half the angle off the wind.
    float     spreadScale = 0.0f;  ///< What makes the spread one round the circle.

    /**
     * @brief The spectrum of @p shape's wind, fetch, depth and spread.
     *
     * @param shape The authored sea's shape.
     * @return Its spectrum; none (any() false) for a windless ocean.
     */
    static OceanSpectrum of(const OceanShape& shape);

    /// Whether there are any waves: wind over some fetch.
    bool any() const { return alpha > 0.0f; }

    /// Height variance a square radian a metre of wavenumber @p k holds.
    float density(const glm::vec2& k) const;

    /// The variance of the slope the waves between wavenumbers @p from and @p to raise.
    float slopeVariance(float from, float to) const;

    /// Radians a second for a wave of @p k radians a metre over @p depth metres (0 deep).
    static double frequencyOf(double k, double depth);

    /// The wavenumber with frequency @p omega over @p depth metres: frequencyOf's inverse.
    static double wavenumberOf(double omega, double depth);

    /// How fast frequencyOf grows with @p k: the speed a group of waves that long travels at.
    static double groupSpeed(double k, double depth);

    /**
     * @brief JONSWAP's energy density at @p omega radians a second, with TMA's shallow share: the
     *        spectrum over frequency, before its spread.
     *
     * @param omega Frequency, radians a second.
     * @return Square metres per radian a second.
     */
    double energy(double omega) const;
};

} // namespace Vkm::Engine
