#include "frame/gl_ocean.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "gl_buffer_upload.h"
#include "system/ocean/ocean_data.h"

namespace Vkm::Engine {

namespace {

// A ripple field repeats every this many of its longest ripples: not whole, so no field's repeat
// lines up with another's.
constexpr float TILE_RATIO = 7.93f;

// Spreads of a squeeze a crest rarely passes, where squeezes add as independent terms.
constexpr float RARE_SPREADS = 3.0f;

} // namespace

GLOcean::GLOcean()  = default;
GLOcean::~GLOcean() = default;

void GLOcean::update(const OceanData& ocean) {
    OceanUBO data{};
    if (ocean.present) {
        const OceanSurface& surface = ocean.surface;
        data.place  = glm::vec4(surface.centre, surface.calmRadius, surface.stillRadius);
        data.detail = glm::vec4(ocean.detailDrift, 1.0f / ocean.detailScale, float(surface.count));
        data.foam   = glm::vec4(ocean.foamColor, ocean.foam);
        // Waves of unrelated phase squeeze the surface as a sum of independent terms: its spread is
        // the root of their halved squares.
        float inStep  = 0.0f;
        float squares = 0.0f;
        for (uint32_t i = 0; i < surface.count; ++i) {
            const GerstnerWave& wave = surface.waves[i];
            data.waves[2 * i]     = glm::vec4(wave.direction, wave.wavenumber, wave.phase);
            data.waves[2 * i + 1] = glm::vec4(wave.amplitude, wave.steepness, wave.frequency, 0.0f);
            const float squeeze = wave.steepness * wave.wavenumber * wave.amplitude;
            inStep  += squeeze;
            squares += 0.5f * squeeze * squeeze;
        }
        const float waves = std::min(inStep, RARE_SPREADS * std::sqrt(squares));
        for (uint32_t i = 0; i < Ocean::MAX_SURGES; ++i) {
            const OceanSurge& surge = surface.surges[i];
            data.surges[2 * i]     = glm::vec4(surge.centre, surge.direction);
            data.surges[2 * i + 1] = glm::vec4(surge.height, surge.halfWidth, surge.halfLength, 0.0f);
        }

        // The ripples on RIPPLE_FIELDS grids, their band split evenly in log, each field TILE_RATIO
        // of its longest ripple across. Their squeeze is choppiness times |k| h, its spread the
        // root of choppiness squared times the slope's variance.
        const OceanRipples& on = ocean.ripples;
        float ripples = 0.0f;
        if (on.any()) {
            // A field's band ends below its grid's last whole bin, half a field across, so its
            // ripples stay their own conjugates' mirror: past that the band is cut short.
            const float lastBin  = float(RIPPLE_FIELD_SIZE / 2 - 1);
            const float widest   = std::pow(lastBin / TILE_RATIO, float(RIPPLE_FIELDS));
            const float shortest = std::min(on.shortest, on.longest * widest);
            for (int f = 0; f < RIPPLE_FIELDS; ++f) {
                data.bands[f] = on.longest * std::pow(shortest / on.longest, float(f) / RIPPLE_FIELDS);
                data.tiles[f] = data.bands[f] / (glm::two_pi<float>() * TILE_RATIO);
            }
            data.bands.w  = shortest;
            data.spectrum = glm::vec4(on.peak, on.alpha, on.depth, on.heightScale);
            data.spread   = glm::vec4(on.wind, on.spreadPower, on.spreadScale);
            const float turn = float(glm::two_pi<double>() / OceanRipples::LOOP);
            data.rippling = glm::vec4(on.cycle, turn, on.choppiness, float(on.seed));
            // The slope's variance is the spectrum's and the band's alone, and costs a sum over the
            // band (OceanRipples::slopeVariance): summed again only when one of them changes.
            if (data.spectrum != m_rippleSpectrum || data.bands != m_rippleBands) {
                m_rippleSpectrum = data.spectrum;
                m_rippleBands    = data.bands;
                m_rippleSlope    = std::sqrt(on.slopeVariance(on.longest, shortest));
            }
            ripples = RARE_SPREADS * on.choppiness * m_rippleSlope;
        }
        const float fields = on.any() ? float(RIPPLE_FIELDS) : 0.0f;
        data.squeeze = glm::vec4(waves, fields, surface.significantHeight(), ripples);
        data.tiles.w = ocean.waterline;
    }

    Vkm::GL::uploadIfChanged(m_ubo, m_last, data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::OCEAN);
}

} // namespace Vkm::Engine
