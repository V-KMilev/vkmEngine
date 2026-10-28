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

} // namespace

GLOcean::GLOcean()  = default;
GLOcean::~GLOcean() = default;

void GLOcean::update(const OceanData& ocean, bool waterShown) {
    OceanUBO data{};
    float waves = 0.0f;
    if (ocean.present) {
        const OceanSurface& surface = ocean.surface;
        data.place  = glm::vec4(surface.centre, surface.calmRadius, surface.stillRadius);
        data.detail = glm::vec4(ocean.detailDrift, 1.0f / ocean.detailScale, 0.0f);
        data.foam   = glm::vec4(ocean.foamColor, ocean.foam);
        // The swell's cascades and moment; its squeeze a crest rarely passes,
        // OceanSwell::RARE_SPREADS of its spread.
        if (const SwellMoment* swell = surface.swell.get()) {
            const OceanSwell& spectrum = swell->swell();
            // Each cascade's size in data.swell's xyz, which hold MAX_SWELL_CASCADES.
            const uint32_t cascades = std::min(spectrum.cascadeCount(), uint32_t(MAX_SWELL_CASCADES));
            data.detail.w = float(cascades);
            for (uint32_t c = 0; c < cascades; ++c) data.swell[c] = spectrum.cascade(c).size;
            data.swell.w = swell->cycle();
            waves = OceanSwell::RARE_SPREADS * spectrum.squeezeSpread();
        }
        for (uint32_t i = 0; i < Ocean::MAX_SURGES; ++i) {
            const OceanSurge& surge = surface.surges[i];
            data.surges[2 * i]     = glm::vec4(surge.centre, surge.direction);
            data.surges[2 * i + 1] = glm::vec4(surge.height, surge.halfWidth, surge.halfLength, 0.0f);
        }
        data.tiles.w = ocean.waterline;
    }

    // The ripples on RIPPLE_FIELDS grids, their band split evenly in log, each field TILE_RATIO
    // of its longest ripple across. Their squeeze is choppiness times |k| h, its spread the root of
    // choppiness squared times the slope's variance.
    const OceanRipples& on = ocean.ripples;
    float ripples = 0.0f;
    const bool wanted = rippling(ocean, waterShown);
    if (wanted) {
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
        const OceanSpectrum& spectrum = on.spectrum;
        data.spectrum = glm::vec4(spectrum.peak, spectrum.alpha, spectrum.depth, spectrum.heightScale);
        data.spread   = glm::vec4(spectrum.wind, spectrum.spreadPower, spectrum.spreadScale);
        const float turn = float(glm::two_pi<double>() / OceanSpectrum::LOOP);
        data.rippling = glm::vec4(on.cycle, turn, on.choppiness, float(on.seed));
        // The slope's variance is the spectrum's and the band's alone, and costs a sum over the
        // band (OceanSpectrum::slopeVariance): summed again only when one of them changes.
        if (data.spectrum != m_rippleSpectrum || data.bands != m_rippleBands) {
            m_rippleSpectrum = data.spectrum;
            m_rippleBands    = data.bands;
            m_rippleSlope    = std::sqrt(spectrum.slopeVariance(on.longest, shortest));
        }
        ripples = OceanSwell::RARE_SPREADS * on.choppiness * m_rippleSlope;
    }
    const float fields      = wanted ? float(RIPPLE_FIELDS) : 0.0f;
    const float significant = ocean.present ? ocean.surface.significantHeight() : 0.0f;
    data.squeeze = glm::vec4(waves, fields, significant, ripples);

    Vkm::GL::uploadIfChanged(m_ubo, m_last, data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::OCEAN);
}

bool GLOcean::rippling(const OceanData& ocean, bool waterShown) {
    return ocean.ripples.any() && (ocean.present || waterShown);
}

} // namespace Vkm::Engine
