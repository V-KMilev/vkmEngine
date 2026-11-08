#include "storage/gl_exposure.h"

#include <algorithm>

#include "vkm_opengl/core/context.h"

#include "storage/gl_target.h"
#include "convention/gl_bindings.h"
#include "io/project/render_settings.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t HISTOGRAM_BYTES = Exposure::BINS * sizeof(uint32_t);

} // namespace

GLExposure::GLExposure()
    : m_histogram("shaders/opengl/post/exposure/histogram")
    , m_bins(nullptr, HISTOGRAM_BYTES)
    , m_readbacks{
        Vkm::OpenGL::ReadbackBuffer(HISTOGRAM_BYTES),
        Vkm::OpenGL::ReadbackBuffer(HISTOGRAM_BYTES),
        Vkm::OpenGL::ReadbackBuffer(HISTOGRAM_BYTES),
    } {}

GLExposure::~GLExposure() = default;

float GLExposure::stops(const RenderSettings& settings, float seconds) {
    if (!settings.autoExposure) dropPending();

    // Oldest first, so the last read is the newest; a copy still in flight holds the rest back.
    const uint32_t* newest = nullptr;
    for (int i = 0; i < READBACKS; ++i) {
        Vkm::OpenGL::ReadbackBuffer& readback = m_readbacks[(m_next + i) % READBACKS];
        if (!readback.pending()) continue;
        if (!readback.arrived()) break;
        newest = static_cast<const uint32_t*>(readback.data());
    }
    return m_adaptation.update(newest, seconds, settings);
}

void GLExposure::measure(const GLTarget& scene, uint32_t width, uint32_t height, float seconds) {
    m_owed = std::min(m_owed + seconds * MEASURE_RATE, 1.0f);
    Vkm::OpenGL::ReadbackBuffer& readback = m_readbacks[m_next];
    if (m_owed < 1.0f || readback.pending()) return;
    m_owed = 0.0f;

    namespace Groups = GLBindings::ComputeGroups;
    namespace SSBO   = GLBindings::SSBOBindingPoints;
    m_bins.clear(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    m_bins.bindBase(SSBO::EXPOSURE_HISTOGRAM);
    scene.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
    m_histogram.bind();
    const auto stride = static_cast<uint32_t>(Exposure::STRIDE);
    m_histogram.dispatch(
        Groups::covering((width + stride - 1) / stride, Groups::IMAGE),
        Groups::covering((height + stride - 1) / stride, Groups::IMAGE)
    );

    Vkm::OpenGL::memoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    readback.capture(m_bins);
    m_next = (m_next + 1) % READBACKS;
}

void GLExposure::forget() {
    dropPending();
    m_adaptation.forget();
}

void GLExposure::dropPending() {
    for (Vkm::OpenGL::ReadbackBuffer& readback : m_readbacks) readback.drop();
}

} // namespace Vkm::Engine
