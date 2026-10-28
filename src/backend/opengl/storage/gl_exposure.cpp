#include "storage/gl_exposure.h"

#include "gl_context.h"

#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_settings.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t HISTOGRAM_BYTES = Exposure::BINS * sizeof(uint32_t);

} // namespace

GLExposure::GLExposure()
    : m_histogram("shaders/exposure/histogram")
    , m_bins(nullptr, HISTOGRAM_BYTES)
    , m_readbacks{
        Vkm::GL::ReadbackBuffer(HISTOGRAM_BYTES),
        Vkm::GL::ReadbackBuffer(HISTOGRAM_BYTES),
        Vkm::GL::ReadbackBuffer(HISTOGRAM_BYTES),
    } {}

GLExposure::~GLExposure() = default;

float GLExposure::stops(const RenderSettings& settings, float seconds) {
    if (!settings.autoExposure) dropPending();

    // Oldest first, so the last read is the newest; a copy still in flight holds the rest back.
    const uint32_t* newest = nullptr;
    for (int i = 0; i < READBACKS; ++i) {
        Vkm::GL::ReadbackBuffer& readback = m_readbacks[(m_next + i) % READBACKS];
        if (!readback.pending()) continue;
        if (!readback.arrived()) break;
        newest = static_cast<const uint32_t*>(readback.data());
    }
    return m_adaptation.update(newest, seconds, settings);
}

void GLExposure::measure(const GLTarget& scene, uint32_t width, uint32_t height) {
    Vkm::GL::ReadbackBuffer& readback = m_readbacks[m_next];
    if (readback.pending()) return;

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

    Vkm::GL::memoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    readback.capture(m_bins);
    m_next = (m_next + 1) % READBACKS;
}

void GLExposure::forget() {
    dropPending();
    m_adaptation.forget();
}

void GLExposure::dropPending() {
    for (Vkm::GL::ReadbackBuffer& readback : m_readbacks) readback.drop();
}

} // namespace Vkm::Engine
