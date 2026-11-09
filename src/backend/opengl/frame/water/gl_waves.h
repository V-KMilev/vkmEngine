#pragma once

#include <cstddef>
#include <memory>

#include <glm/glm.hpp>

#include "convention/gl_bindings.h"
#include "storage/water/gl_wave_fields.h"
#include "ecs/settings/wave_settings.h"
#include "ecs/water/swell.h"

namespace Vkm::OpenGL {
    class UniformBuffer;
}

namespace Vkm::Engine {

struct WaveFrame;

static_assert(RIPPLE_FIELDS == 3, "WaveBlock packs a field's repeat and band into xyz of a vec4");
static_assert(SWELL_FIELD == 2 * GLBindings::ComputeGroups::SWELL_FFT, "A swell line's transform takes a butterfly a thread (shaders/opengl/scene/water/swell/swell.glsl)");
static_assert(MAX_SWELL_CASCADES <= 3, "WaveBlock packs a cascade's size into xyz of a vec4");

/**
 * @brief std140 layout - must match WaveBlock in shaders/opengl/include/waves.glsl.
 *
 * The swell's grids lie in SSBO_SWELL (GLWaveFields), drawn at `swell`'s moment. The ripples
 * are Ripples' spectrum, drawn on RIPPLE_FIELDS repeating grids of wavenumbers
 * (GLWaveFields), the longer ones over the larger repeat; their band is split evenly in log.
 */
struct WaveUBO {
    glm::vec4 place;                                 ///< xy = the center on the XZ plane, z = calm radius, w
                                                     ///< = still radius.
    glm::vec4 detail;                                ///< xy = the maps' drift in repeats, z = repeats a
                                                     ///< meter, w = swell cascades.
    glm::vec4 squeeze;                               ///< x = a squeeze (1 - the Jacobian) the waves' crests
                                                     ///< rarely pass, y = ripple fields (0 none), z = the
                                                     ///< waves' significant height, w = the squeeze the
                                                     ///< ripples rarely add.
    glm::vec4 tiles;                                 ///< xyz = repeats a meter of the long, middle and short
                                                     ///< ripples' fields; w unused.
    glm::vec4 bands;                                 ///< xyz = the lowest wavenumber of each ripple field, w
                                                     ///< = the highest of the last.
    glm::vec4 spectrum;                              ///< x = JONSWAP's peak, radians a second; y = its alpha
                                                     ///< g^2; z = depth, 0 deep; w = height scale.
    glm::vec4 spread;                                ///< xy = the way the waves roll, unit; z = the spread's
                                                     ///< power s; w = what makes it one round the circle.
    glm::vec4 rippling;                              ///< x = how far through their loop, 0..1; y = radians a
                                                     ///< second a loop's turn; z = choppiness; w = seed.
    glm::vec4 swell;                                 ///< xyz = meters across each swell cascade, w = how far
                                                     ///< through the loop, 0..1.
    glm::vec4 surges[2 * WaveSettings::MAX_SURGES];  ///< A surge takes two vec4s: center and direction; then
                                                     ///< height (0 none), half its width along its roll,
                                                     ///< half its length along its crest.
};

static_assert(offsetof(WaveUBO, swell) == 128, "WaveBlock: swell");
static_assert(offsetof(WaveUBO, surges) == 144, "WaveBlock: surges");
static_assert(sizeof(WaveUBO) == 144 + 32 * WaveSettings::MAX_SURGES, "WaveBlock: size");

/**
 * @brief GPU mirror of the scene's waves - the WaveBlock UBO water's vertices move by.
 *
 * update() skips the upload when nothing changed; with no swell, water lies still but for its surges.
 * The backend's frame owns the one GLWaves; captures draw water without waves.
 */
class GLWaves {
    public:
        GLWaves();
        ~GLWaves();

        GLWaves(const GLWaves& other) = delete;
        GLWaves& operator=(const GLWaves& other) = delete;

        GLWaves(GLWaves && other) = delete;
        GLWaves& operator=(GLWaves && other) = delete;

    public:
        /**
         * @brief Upload @p waves and bind them.
         *
         * @param waves      The frame's waves.
         * @param waterShown Whether water is drawn: the ripples go up only then.
         */
        void update(const WaveFrame& waves, bool waterShown = false);

        /**
         * @brief Whether a frame draws the ripples: all water shows them, so wanted while any is
         *        drawn and the wind raises any.
         *
         * @param waves      The frame's waves.
         * @param waterShown Whether water is drawn.
         * @return Whether the block holds ripple fields to read.
         */
        static bool rippling(const WaveFrame& waves, bool waterShown);

    private:
        std::unique_ptr<Vkm::OpenGL::UniformBuffer> m_ubo;
        WaveUBO                                     m_last{};

        float     m_rippleSlope    = 0.0f;  ///< The ripples' slope deviation, the root of its variance, and
                                            ///< the spectrum and band it was summed for.
        glm::vec4 m_rippleSpectrum = glm::vec4(-1.0f);
        glm::vec4 m_rippleBands    = glm::vec4(-1.0f);
};

} // namespace Vkm::Engine
