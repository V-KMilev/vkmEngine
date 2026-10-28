#include "storage/gl_ocean_fields.h"

#include <algorithm>
#include <vector>

#include <GL/glew.h>

#include "gl_context.h"

#include "gl_mip_levels.h"
#include "convention/gl_bindings.h"
#include "frame/gl_ocean.h"
#include "system/ocean/ocean_data.h"

namespace Vkm::Engine {

namespace {

namespace Groups = GLBindings::ComputeGroups;

// Anisotropic taps along a grazing view of the sea.
constexpr float MAX_ANISOTROPY = 8.0f;

} // namespace

GLOceanFields::GLOceanFields()
    : m_swellRows("shaders/ocean/swell")
    , m_swellColumns("shaders/ocean/swell_columns")
    , m_ripples("shaders/ocean/ripples")
    , m_ripplesColumns("shaders/ocean/ripples_columns")
    , m_ripplesDown("shaders/ocean/ripples_down") {}

GLOceanFields::~GLOceanFields() = default;

void GLOceanFields::beginFrame(const OceanData& ocean) {
    m_rippled = false;
    m_swelled = false;
    if (ocean.present) drawSwell(ocean);
}

void GLOceanFields::drawSwell(const OceanData& ocean) {
    const SwellMoment* moment = ocean.surface.swell.get();
    if (!moment) return;
    const OceanSwell& swell = moment->swell();
    constexpr uint32_t TEXELS   = uint32_t(SWELL_FIELD) * SWELL_FIELD;
    const uint32_t     cascades = swell.cascadeCount();

    // A new swell: its spectrum up, and its grids' foam from none.
    if (swell.version() != m_swellVersion) {
        std::vector<glm::vec4> spectrum(size_t(TEXELS) * cascades);
        for (uint32_t c = 0; c < cascades; ++c) {
            const SwellBin* bins = swell.bins(c);
            for (uint32_t i = 0; i < TEXELS; ++i) {
                spectrum[size_t(c) * TEXELS + i] = glm::vec4(bins[i].height, bins[i].turns, 0.0f);
            }
        }
        const auto bytes = uint32_t(spectrum.size() * sizeof(glm::vec4));
        m_swellSpectrum  = std::make_unique<Vkm::GL::ShaderStorageBuffer>(spectrum.data(), bytes);
        if (!m_swell) {
            // Every cascade's texels, two vec4s each, then their foam (shaders/ocean.glsl).
            const auto texel = uint32_t(2 * sizeof(glm::vec4) + sizeof(float));
            const auto grids = uint32_t(MAX_SWELL_CASCADES) * TEXELS * texel;
            m_swell = std::make_unique<Vkm::GL::ShaderStorageBuffer>(nullptr, grids);
        }
        m_swell->clear(GL_R32F, GL_RED, GL_FLOAT, nullptr);
        m_swellVersion = swell.version();
        m_foamTime     = ocean.surface.time;
    }
    // The sea's seconds since the last draw, not the frame's: paused, the foam stays as the waves do.
    const auto shed = static_cast<float>(std::max(ocean.surface.time - m_foamTime, 0.0));
    m_foamTime      = ocean.surface.time;

    namespace SSBO = GLBindings::SSBOBindingPoints;
    m_swellSpectrum->bindBase(SSBO::OCEAN_SWELL_SPECTRUM);
    m_swell->bindBase(SSBO::OCEAN_SWELL);
    m_swellRows.bind();
    m_swellRows.setUniform1f("u_choppiness", swell.choppiness());
    m_swellRows.dispatch(1, uint32_t(SWELL_FIELD), cascades);
    Vkm::GL::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    m_swellColumns.bind();
    m_swellColumns.setUniform1f("u_seconds", shed);
    m_swellColumns.dispatch(uint32_t(SWELL_FIELD), 1, cascades);
    Vkm::GL::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    m_swelled = true;
}

void GLOceanFields::drawRipples() {
    if (!m_fields) {
        Vkm::GL::Texture2DArrayParams rows;
        rows.width          = RIPPLE_FIELD_SIZE;
        rows.height         = RIPPLE_FIELD_SIZE;
        rows.layers         = RIPPLE_FIELDS;
        rows.internalFormat = GL_RGBA32F;
        m_rows = std::make_unique<Vkm::GL::Texture2DArray>("ocean_ripple_rows", rows);

        Vkm::GL::Texture2DArrayParams fields = rows;
        const int levels      = mipLevelsDownToTwo(RIPPLE_FIELD_SIZE, RIPPLE_FIELD_SIZE, 16);
        fields.levels         = static_cast<uint32_t>(levels);
        fields.internalFormat = GL_RGBA16F;
        fields.wrapS          = Vkm::GL::TextureWrap::Repeat;
        fields.wrapT          = Vkm::GL::TextureWrap::Repeat;
        fields.minFilter      = Vkm::GL::TextureMinFilter::LinearMipmapLinear;
        fields.maxAnisotropy  = MAX_ANISOTROPY;
        m_fields = std::make_unique<Vkm::GL::Texture2DArray>("ocean_ripples", fields);
    }

    // A row of every field a group, then a column: both transforms of a line run in shared memory.
    const uint32_t lines = static_cast<uint32_t>(RIPPLE_FIELD_SIZE);
    const uint32_t layers = static_cast<uint32_t>(RIPPLE_FIELDS);
    m_ripples.bind();
    m_rows->bindImage(0, GL_WRITE_ONLY);
    m_ripples.dispatch(1, lines, layers);
    Vkm::GL::memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    m_ripplesColumns.bind();
    m_rows->bindImage(0, GL_READ_ONLY);
    m_fields->bindImage(1, GL_WRITE_ONLY);
    m_ripplesColumns.dispatch(lines, 1, layers);
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    // The levels below, every layer at a dispatch: the driver's own mip build costs more.
    m_ripplesDown.bind();
    m_fields->bindSlot(0);
    for (uint32_t level = 1; level < m_fields->levels(); ++level) {
        const uint32_t size = static_cast<uint32_t>(RIPPLE_FIELD_SIZE >> level);
        m_ripplesDown.setUniform1i("u_above", level - 1);
        m_fields->bindImage(0, GL_WRITE_ONLY, level);
        m_ripplesDown.dispatch(
            Groups::covering(size, Groups::IMAGE),
            Groups::covering(size, Groups::IMAGE),
            static_cast<uint32_t>(RIPPLE_FIELDS)
        );
        Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    m_rippled = true;
}

void GLOceanFields::bind() const {
    if (m_swelled) m_swell->bindBase(GLBindings::SSBOBindingPoints::OCEAN_SWELL);
    if (m_rippled) m_fields->bindSlot(GLBindings::OceanTextureSlots::RIPPLES);
}

} // namespace Vkm::Engine
