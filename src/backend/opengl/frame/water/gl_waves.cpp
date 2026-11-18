#include "frame/water/gl_waves.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "vkm_opengl/buffer/buffer_upload.h"
#include "vkm_opengl/buffer/uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "ecs/water/wave_frame.h"

namespace Vkm::Engine {

namespace {

// A ripple field repeats every this many of its longest ripples: not whole, so no field's repeat
// lines up with another's.
constexpr float TILE_RATIO = 7.93f;

} // namespace

GLWaves::GLWaves()  = default;
GLWaves::~GLWaves() = default;

void GLWaves::update(const WaveFrame& waves, bool waterShown) {
    WaveUBO data{};
    float squeeze = 0.0f;
    const WaveSurface& surface = waves.surface;
    data.place  = glm::vec4(surface.center, surface.calmRadius, surface.stillRadius);
    data.detail = glm::vec4(waves.detailDrift, 1.0f / WaveSettings::DETAIL_SCALE, 0.0f);
    // The swell's cascades and moment; its squeeze a crest rarely passes,
    // Swell::RARE_SPREADS of its spread.
    if (const SwellMoment* swell = surface.swell.get()) {
        const Swell& spectrum = swell->swell();
        // Each cascade's size in data.swell's xyz, which hold MAX_SWELL_CASCADES.
        const uint32_t cascades = std::min(spectrum.cascadeCount(), uint32_t(MAX_SWELL_CASCADES));
        data.detail.w = float(cascades);
        for (uint32_t c = 0; c < cascades; ++c) data.swell[c] = spectrum.cascade(c).size;
        data.swell.w = swell->cycle();
        squeeze      = Swell::RARE_SPREADS * spectrum.squeezeSpread();
    }
    for (uint32_t i = 0; i < WaveSettings::MAX_SURGES; ++i) {
        const WaveSurge& surge = surface.surges[i];
        data.surges[2 * i]     = glm::vec4(surge.center, surge.direction);
        data.surges[2 * i + 1] = glm::vec4(surge.height, surge.halfWidth, surge.halfLength, 0.0f);
    }

    // The ripples on RIPPLE_FIELDS grids, their band split evenly in log, each field TILE_RATIO
    // of its longest ripple across. Their squeeze is choppiness times |k| h, its spread the root of
    // choppiness squared times the slope's variance.
    const Ripples& on = waves.ripples;
    float ripples     = 0.0f;
    const bool wanted = rippling(waves, waterShown);
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
        const WaveSpectrum& spectrum = on.spectrum;
        const float turn = float(glm::two_pi<double>() / WaveSpectrum::LOOP);

        data.bands.w  = shortest;
        data.spectrum = glm::vec4(spectrum.peak, spectrum.alpha, spectrum.depth, spectrum.heightScale);
        data.spread   = glm::vec4(spectrum.wind, spectrum.spreadPower, spectrum.spreadScale);
        data.rippling = glm::vec4(on.cycle, turn, on.choppiness, float(on.seed));
        // The slope's variance is the spectrum's and the band's alone, and costs a sum over the
        // band (WaveSpectrum::slopeVariance): summed again only when one of them changes.
        if (data.spectrum != m_rippleSpectrum || data.bands != m_rippleBands) {
            m_rippleSpectrum = data.spectrum;
            m_rippleBands    = data.bands;
            m_rippleSlope    = std::sqrt(spectrum.slopeVariance(on.longest, shortest));
        }
        ripples = Swell::RARE_SPREADS * on.choppiness * m_rippleSlope;
    }
    const float fields = wanted ? float(RIPPLE_FIELDS) : 0.0f;
    data.squeeze = glm::vec4(squeeze, fields, surface.significantHeight(), ripples);

    Vkm::OpenGL::uploadIfChanged(m_ubo, m_last, data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::WAVES);
}

bool GLWaves::rippling(const WaveFrame& waves, bool waterShown) {
    return waves.ripples.any() && waterShown;
}

} // namespace Vkm::Engine
