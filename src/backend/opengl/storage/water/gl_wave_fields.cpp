#include "storage/water/gl_wave_fields.h"

#include <algorithm>
#include <vector>

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"

#include "convention/gl_mip_levels.h"
#include "gl_profiler.h"
#include "convention/gl_bindings.h"
#include "ecs/water/wave_frame.h"
#include "ecs/water/swell.h"

namespace Vkm::Engine {

namespace {

namespace Groups = GLBindings::ComputeGroups;

// Anisotropic taps along a grazing view of the sea.
constexpr float MAX_ANISOTROPY = 8.0f;

} // namespace

GLWaveFields::GLWaveFields()
    : m_swellRows("shaders/opengl/scene/water/swell")
    , m_swellColumns("shaders/opengl/scene/water/swell/columns")
    , m_ripples("shaders/opengl/scene/water/ripples")
    , m_ripplesColumns("shaders/opengl/scene/water/ripples/columns")
    , m_ripplesDown("shaders/opengl/scene/water/ripples/down") {}

GLWaveFields::~GLWaveFields() = default;

void GLWaveFields::beginFrame(const WaveFrame& waves, bool hasWater, bool swellShown) {
    if (hasWater) makeFields();
    m_swelled = false;
    if (swellShown) {
        PROFILE_GPU_SCOPE("Water/Swell");
        drawSwell(waves);
    }
}

void GLWaveFields::drawSwell(const WaveFrame& waves) {
    const SwellMoment* moment = waves.surface.swell.get();
    if (!moment) return;
    const Swell& swell = moment->swell();
    constexpr uint32_t TEXELS = uint32_t(SWELL_FIELD) * SWELL_FIELD;
    const uint32_t cascades = swell.cascadeCount();

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
        m_swellSpectrum = std::make_unique<Vkm::OpenGL::ShaderStorageBuffer>(spectrum.data(), bytes);
        if (!m_swell) {
            // Every cascade's texels, two vec4s each, then their foam (shaders/opengl/include/waves.glsl).
            const auto texel = uint32_t(2 * sizeof(glm::vec4) + sizeof(float));
            const auto grids = uint32_t(MAX_SWELL_CASCADES) * TEXELS * texel;
            m_swell = std::make_unique<Vkm::OpenGL::ShaderStorageBuffer>(nullptr, grids);
        }
        m_swell->clear(GL_R32F, GL_RED, GL_FLOAT, nullptr);
        m_swellVersion = swell.version();
        m_foamTime     = waves.surface.time;
    }
    // The waves' seconds since the last draw, not the frame's: paused, the foam stays as they do.
    const auto shed = static_cast<float>(std::max(waves.surface.time - m_foamTime, 0.0));
    m_foamTime = waves.surface.time;

    namespace SSBO = GLBindings::SSBOBindingPoints;
    m_swellSpectrum->bindBase(SSBO::SWELL_SPECTRUM);
    m_swell->bindBase(SSBO::SWELL);
    m_swellRows.bind();
    m_swellRows.setUniform1f("u_choppiness", swell.choppiness());
    m_swellRows.dispatch(1, uint32_t(SWELL_FIELD), cascades);
    Vkm::OpenGL::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    m_swellColumns.bind();
    m_swellColumns.setUniform1f("u_seconds", shed);
    m_swellColumns.dispatch(uint32_t(SWELL_FIELD), 1, cascades);
    Vkm::OpenGL::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    m_swelled = true;
}

void GLWaveFields::makeFields() {
    if (!m_fields) {
        Vkm::OpenGL::Texture2DArrayParams rows;
        rows.width          = RIPPLE_FIELD_SIZE;
        rows.height         = RIPPLE_FIELD_SIZE;
        rows.layers         = RIPPLE_FIELDS;
        rows.internalFormat = GL_RGBA32F;
        m_rows              = std::make_unique<Vkm::OpenGL::Texture2DArray>("water_ripple_rows", rows);

        Vkm::OpenGL::Texture2DArrayParams fields = rows;
        const int levels = mipLevelsDownToTwo(RIPPLE_FIELD_SIZE, RIPPLE_FIELD_SIZE, 16);
        fields.levels         = static_cast<uint32_t>(levels);
        fields.internalFormat = GL_RGBA16F;
        fields.wrapS          = Vkm::OpenGL::TextureWrap::Repeat;
        fields.wrapT          = Vkm::OpenGL::TextureWrap::Repeat;
        fields.minFilter      = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
        fields.maxAnisotropy  = MAX_ANISOTROPY;
        m_fields              = std::make_unique<Vkm::OpenGL::Texture2DArray>("water_ripples", fields);
    }
}

void GLWaveFields::drawRipples(float seconds) {
    makeFields();
    if (!m_rippled || seconds <= 0.0f) {
        for (uint32_t field = 0; field < RIPPLE_FIELDS; ++field) {
            drawField(field);
            m_owed[field] = -static_cast<float>(field) / static_cast<float>(RIPPLE_FIELDS);
        }
        m_rippled = true;
        return;
    }
    // Due from half a draw, so a frame rate near FIELD_RATE draws every frame rather than skipping
    // one as a frame comes in a little early; a hitch is not made up.
    for (uint32_t field = 0; field < RIPPLE_FIELDS; ++field) {
        m_owed[field] = std::min(m_owed[field] + seconds * FIELD_RATE, 1.5f);
        if (m_owed[field] < 0.5f) continue;
        m_owed[field] -= 1.0f;
        drawField(field);
    }
}

void GLWaveFields::drawField(uint32_t field) {
    // A row a group, then a column: both transforms of a line run in shared memory.
    const uint32_t lines = static_cast<uint32_t>(RIPPLE_FIELD_SIZE);
    m_ripples.bind();
    m_ripples.setUniform1i("u_field", static_cast<int>(field));
    m_rows->bindImage(0, GL_WRITE_ONLY);
    m_ripples.dispatch(1, lines);
    Vkm::OpenGL::memoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    m_ripplesColumns.bind();
    m_ripplesColumns.setUniform1i("u_field", static_cast<int>(field));
    m_rows->bindImage(0, GL_READ_ONLY);
    m_fields->bindImage(1, GL_WRITE_ONLY);
    m_ripplesColumns.dispatch(lines, 1);
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

    // The levels below, a dispatch each: the driver's own mip build costs more.
    m_ripplesDown.bind();
    m_ripplesDown.setUniform1i("u_field", static_cast<int>(field));
    m_fields->bindSlot(GLBindings::WaterTextureSlots::RIPPLES_ABOVE);
    for (uint32_t level = 1; level < m_fields->levels(); ++level) {
        const uint32_t size = static_cast<uint32_t>(RIPPLE_FIELD_SIZE >> level);
        m_ripplesDown.setUniform1i("u_above", level - 1);
        m_fields->bindImage(0, GL_WRITE_ONLY, level);
        m_ripplesDown.dispatch(Groups::covering(size, Groups::IMAGE), Groups::covering(size, Groups::IMAGE));
        Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
    }
}

void GLWaveFields::bind() const {
    if (m_swelled) m_swell->bindBase(GLBindings::SSBOBindingPoints::SWELL);
    if (m_rippled) m_fields->bindSlot(GLBindings::WaterTextureSlots::RIPPLES);
}

} // namespace Vkm::Engine
