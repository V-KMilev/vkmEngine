#pragma once

#include <cstddef>
#include <memory>

#include <glm/glm.hpp>

#include "convention/gl_bindings.h"
#include "ecs/component/render/ocean.h"
#include "system/ocean/ocean_swell.h"

namespace Vkm::GL {
    class UniformBuffer;
}

namespace Vkm::Engine {

struct OceanData;

/// The ripples' repeating fields (GLOceanFields), longest ripples first.
constexpr int RIPPLE_FIELDS = 3;
static_assert(RIPPLE_FIELDS == 3, "OceanBlock packs a field's repeat and band into xyz of a vec4");
/// Texels across a ripple field, a butterfly a thread for half of them (shaders/ocean_ripples.glsl).
constexpr int RIPPLE_FIELD_SIZE = 2 * GLBindings::ComputeGroups::OCEAN_FFT;
static_assert(
    SWELL_FIELD == 2 * GLBindings::ComputeGroups::SWELL_FFT,
    "A swell line's transform takes a butterfly a thread (shaders/ocean_swell.glsl)"
);
static_assert(MAX_SWELL_CASCADES <= 3, "OceanBlock packs a cascade's size into xyz of a vec4");

/**
 * @brief std140 layout - must match OceanBlock in shaders/ocean.glsl.
 *
 * The swell's grids lie in SSBO_OCEAN_SWELL (GLOceanFields), drawn at `swell`'s moment. The ripples
 * are OceanRipples' spectrum, drawn on RIPPLE_FIELDS repeating grids of wavenumbers
 * (GLOceanFields), the longer ones over the larger repeat; their band is split evenly in log.
 */
struct OceanUBO {
    glm::vec4 place;    ///< xy = the centre on the XZ plane, z = calm radius, w = still radius.
    /// xy = the maps' drift in repeats, z = repeats a metre (0 only with no ocean), w = swell cascades.
    glm::vec4 detail;
    glm::vec4 foam;     ///< rgb = color, a = how readily a crest whitens.
    /// x = a squeeze (1 - the Jacobian) the waves' crests rarely pass, y = ripple fields (0 none),
    /// z = the waves' significant height, w = the squeeze the ripples rarely add.
    glm::vec4 squeeze;
    /// xyz = repeats a metre of the long, middle and short ripples' fields; w = metres of waterline foam.
    glm::vec4 tiles;
    /// xyz = the lowest wavenumber of each ripple field, w = the highest of the last.
    glm::vec4 bands;
    /// x = JONSWAP's peak, radians a second; y = its alpha g^2; z = depth, 0 deep; w = height scale.
    glm::vec4 spectrum;
    /// xy = the way the waves roll, unit; z = the spread's power s; w = what makes it one round the circle.
    glm::vec4 spread;
    /// x = how far through their loop, 0..1; y = radians a second a loop's turn; z = choppiness; w = seed.
    glm::vec4 rippling;
    /// xyz = metres across each swell cascade, w = how far through the loop, 0..1.
    glm::vec4 swell;
    /// A surge takes two vec4s: centre and direction; then height (0 none), half-width, half-length.
    glm::vec4 surges[2 * Ocean::MAX_SURGES];
};

static_assert(offsetof(OceanUBO, swell) == 144, "OceanBlock: swell");
static_assert(offsetof(OceanUBO, surges) == 160, "OceanBlock: surges");
static_assert(sizeof(OceanUBO) == 160 + 32 * Ocean::MAX_SURGES, "OceanBlock: size");

/**
 * @brief GPU mirror of the scene's ocean - the OceanBlock UBO a Sea material's vertices move by.
 *
 * update() skips the upload when nothing changed; with no ocean the block holds no swell, and a
 * Sea material draws as an ordinary one. Each eye drawing into a target of its own owns one,
 * as with GLCamera.
 */
class GLOcean {
    public:
        GLOcean();
        ~GLOcean();

        GLOcean(const GLOcean& other) = delete;
        GLOcean& operator=(const GLOcean& other) = delete;

        GLOcean(GLOcean && other) = delete;
        GLOcean& operator=(GLOcean && other) = delete;

    public:
        /**
         * @brief Upload @p ocean, or a still sea when it is not present, and bind it.
         *
         * @param ocean      The frame's ocean.
         * @param waterShown Whether other water is drawn: its ripples go up with no sea too.
         */
        void update(const OceanData& ocean, bool waterShown = false);

        /**
         * @brief Whether a frame draws the ripples: they are the sea's and every other water's, so
         *        wanted while the scene has an ocean or other water is drawn, and the wind raises any.
         *
         * @param ocean      The frame's ocean.
         * @param waterShown Whether other water is drawn.
         * @return Whether the block holds ripple fields to read.
         */
        static bool rippling(const OceanData& ocean, bool waterShown);

    private:
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        OceanUBO                                m_last{};

        /// The ripples' slope deviation, the root of its variance, and the spectrum and band it
        /// was summed for.
        float     m_rippleSlope    = 0.0f;
        glm::vec4 m_rippleSpectrum = glm::vec4(-1.0f);
        glm::vec4 m_rippleBands    = glm::vec4(-1.0f);
};

} // namespace Vkm::Engine
