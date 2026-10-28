#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>

#include "system/ocean/ocean_spectrum.h"

namespace Vkm::Engine {

struct Ocean;

/// Texels across a swell cascade's grid, and the points of its transform.
inline constexpr int SWELL_FIELD = 64;
/// The most cascades a swell is laid over.
inline constexpr int MAX_SWELL_CASCADES = 3;

/// One grid point of a cascade's spectrum: the wave it holds, as it stood at the loop's start.
struct SwellBin {
    glm::vec2 height = glm::vec2(0.0f);  ///< Tessendorf's h0: complex metres; zero outside the band.
    float     turns  = 0.0f;  ///< Whole turns it rolls a loop (OceanSpectrum::LOOP): its frequency.
};

/// One cascade: a repeating square of the swell, holding its share of the band.
struct SwellCascade {
    float size     = 0.0f;  ///< Metres across one repeat.
    float longest  = 0.0f;  ///< Metres: the longest wave it holds.
    float shortest = 0.0f;  ///< And the shortest, the longest of the next cascade's.
};

/**
 * @brief An ocean's swell: its spectrum, from `maxWavelength` down to `minWavelength`, laid over
 *        up to MAX_SWELL_CASCADES repeating grids, every wavenumber of a grid a wave
 *        (Tessendorf 2001).
 *
 * A cascade holds the waves from its size down to the one SWELL_TEXELS_PER_WAVE texels long, so
 * read between its texels it is all but the waves themselves; the next is that wave across, made
 * SWELL_CASCADE_RATIO larger so no repeat lines up with another's. The last, which reaches
 * minWavelength, repeats at SWELL_FIELD / SWELL_TEXELS_PER_WAVE of those waves across, the most its
 * grid still resolves, so a short last band is not a few dozen waves repeating - or a little less,
 * where that would fall a whole number of times into a larger repeat. Below it, the band is the
 * ripples' (OceanRipples). Built once per change of the sea and shared: SwellMoment draws it at a
 * moment.
 */
class OceanSwell {
    public:
        /// Texels a wave of a cascade spans at the least: read between texels, it holds its shape.
        static constexpr int   SWELL_TEXELS_PER_WAVE = 8;
        /// How much larger than the last cascade's shortest wave the next cascade is across.
        static constexpr float SWELL_CASCADE_RATIO   = 1.27f;
        /// Spreads of the squeeze a crest rarely passes: past it the surface would fold over.
        static constexpr float RARE_SPREADS          = 3.0f;

    public:
        OceanSwell()  = default;
        ~OceanSwell() = default;

        OceanSwell(const OceanSwell& other) = delete;
        OceanSwell& operator=(const OceanSwell& other) = delete;

        OceanSwell(OceanSwell && other) = delete;
        OceanSwell& operator=(OceanSwell && other) = delete;

    public:
        /**
         * @brief The swell of a sea of @p shape.
         *
         * @param shape The authored sea's shape.
         * @return Its swell; null for a windless sea or an empty band.
         */
        static std::shared_ptr<const OceanSwell> of(const OceanShape& shape);

        /**
         * @brief The shortest wave a sea of @p shape's swell holds, in metres: where its ripples begin.
         *
         * @param shape The authored sea's shape.
         * @return `minWavelength`, unless the cascades end short of it.
         */
        static float shortestOf(const OceanShape& shape);

        uint32_t cascadeCount() const { return m_count; }
        const SwellCascade& cascade(uint32_t c) const { return m_cascades[c]; }

        /// Cascade @p c's grid, SWELL_FIELD squared, rows of Z, X across each.
        const SwellBin* bins(uint32_t c) const {
            return m_bins.data() + size_t(c) * SWELL_FIELD * SWELL_FIELD;
        }

        /// How far a crest draws the water toward it: `choppiness`, held where it would fold.
        float choppiness() const { return m_choppiness; }

        /// Spectral significant wave height, 4 sqrt(m0): the mean of the highest third of the waves.
        float significantHeight() const { return m_significantHeight; }

        /// The farthest the swell can carry the water: x across, y up or down, every wave in step.
        glm::vec2 farthestReach() const { return m_farthestReach; }

        /// The spread of the squeeze (1 - the Jacobian, to first order) the swell's crests raise.
        float squeezeSpread() const { return m_squeezeSpread; }

        /// Names this swell among every one made: a new one, a new number.
        uint64_t version() const { return m_version; }

    private:
        uint32_t                                     m_count = 0;
        std::array<SwellCascade, MAX_SWELL_CASCADES> m_cascades{};
        std::vector<SwellBin>                        m_bins;

        float     m_choppiness        = 0.0f;
        float     m_significantHeight = 0.0f;
        glm::vec2 m_farthestReach     = glm::vec2(0.0f);
        float     m_squeezeSpread     = 0.0f;
        uint64_t  m_version           = 0;
};

/// What a cascade's grid holds at a moment, at each texel: where the water resting there has been
/// carried, and how that changes across the rest plane.
struct SwellTexel {
    glm::vec4 moved;   ///< xyz = the carry (X, up, Z), w = its X's change along Z.
    glm::vec4 slopes;  ///< x, y = the height's change along X, Z; z, w = the carry's X along X, Z along Z.
};

/**
 * @brief An ocean's swell at one moment: its cascades' grids, drawn on first asking and read between
 *        their texels.
 *
 * The CPU's statement of the GPU's (GLOceanFields, shaders/ocean_swell.glsl): the same waves at the
 * same moment of the loop, the same transforms and the same reading between texels, so what floats
 * rides what is drawn. A host that asks nothing of the water draws nothing. Safe to ask from
 * several threads.
 */
class SwellMoment {
    public:
        /**
         * @brief The swell @p swell, @p cycle of the way through its loop.
         *
         * @param swell The swell, shared.
         * @param cycle 0..1: how far through OceanSpectrum::LOOP.
         */
        SwellMoment(std::shared_ptr<const OceanSwell> swell, float cycle);
        /// Gives its grids back for the next moment to draw into.
        ~SwellMoment();

        SwellMoment(const SwellMoment& other) = delete;
        SwellMoment& operator=(const SwellMoment& other) = delete;

        SwellMoment(SwellMoment && other) = delete;
        SwellMoment& operator=(SwellMoment && other) = delete;

    public:
        const OceanSwell& swell() const { return *m_swell; }

        /// How far through the loop, 0..1.
        float cycle() const { return m_cycle; }

        /// Where the water resting at @p rest has been carried: X and Z across, Y up.
        glm::vec3 offsetAt(const glm::vec2& rest) const;

        /**
         * @brief The carry at @p rest, and how it changes by rest X and Z.
         *
         * @param rest   A point of the rest plane.
         * @param alongX Gains the carry's change along X.
         * @param alongZ Gains its change along Z.
         * @return The carry.
         */
        glm::vec3 movedAt(const glm::vec2& rest, glm::vec3& alongX, glm::vec3& alongZ) const;

        /// Cascade @p c's grid, drawn on first asking: SWELL_FIELD squared, rows of Z.
        const SwellTexel* grid(uint32_t c) const;

    private:
        /// Draw every cascade's grid.
        void draw() const;

        /// The four texels of cascade @p c round @p rest, and the weights between them.
        void corners(uint32_t c, const glm::vec2& rest, const SwellTexel* at[4], float weight[4]) const;

    private:
        std::shared_ptr<const OceanSwell> m_swell;
        float                             m_cycle = 0.0f;
        mutable std::once_flag            m_drawn;
        mutable std::vector<SwellTexel>   m_grids;  ///< Every cascade's, end to end.
};

} // namespace Vkm::Engine
