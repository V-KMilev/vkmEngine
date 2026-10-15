#include "storage/gl_exposure.h"

#include <stdexcept>

#include "gl_error_handle.h"

#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/render_settings.h"

namespace Vkm::Engine {

namespace {

constexpr GLsizeiptr HISTOGRAM_BYTES = Exposure::BINS * sizeof(uint32_t);
constexpr GLbitfield READ_MAPPED = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

} // namespace

GLExposure::GLExposure()
    : m_histogram("shaders/exposure/histogram")
    , m_bins(nullptr, static_cast<uint32_t>(HISTOGRAM_BYTES))
{
    for (Readback& readback : m_readbacks) {
        VKM_GL_CHECK(glCreateBuffers(1, &readback.buffer));
        VKM_GL_CHECK(glNamedBufferStorage(readback.buffer, HISTOGRAM_BYTES, nullptr, READ_MAPPED));
        readback.bins = static_cast<const uint32_t*>(
            glMapNamedBufferRange(readback.buffer, 0, HISTOGRAM_BYTES, READ_MAPPED)
        );
        if (!readback.bins) throw std::runtime_error("Failed to map an exposure readback buffer");
    }
}

GLExposure::~GLExposure() {
    dropPending();
    for (Readback& readback : m_readbacks) {
        if (readback.buffer != 0) VKM_GL_CHECK(glDeleteBuffers(1, &readback.buffer));
    }
}

float GLExposure::stops(const RenderSettings& settings, float seconds) {
    if (!settings.autoExposure) dropPending();

    // Oldest first, so the last read is the newest; a fence still pending holds the rest back.
    const uint32_t* newest = nullptr;
    for (int i = 0; i < READBACKS; ++i) {
        Readback& readback = m_readbacks[(m_next + i) % READBACKS];
        if (!readback.fence) continue;
        const GLenum status = glClientWaitSync(readback.fence, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) break;
        VKM_GL_CHECK(glDeleteSync(readback.fence));
        readback.fence = nullptr;
        newest         = readback.bins;
    }
    return m_adaptation.update(newest, seconds, settings);
}

void GLExposure::measure(const GLTarget& scene, uint32_t width, uint32_t height) {
    Readback& readback = m_readbacks[m_next];
    if (readback.fence) return;

    namespace Groups = GLBindings::ComputeGroups;
    namespace SSBO   = GLBindings::SSBOBindingPoints;
    const GLuint zero = 0;
    VKM_GL_CHECK(glClearNamedBufferData(m_bins.getID(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero));
    m_bins.bindBase(SSBO::EXPOSURE_HISTOGRAM);
    scene.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::SCENE_COLOR);
    m_histogram.bind();
    const auto stride = static_cast<uint32_t>(Exposure::STRIDE);
    m_histogram.dispatch(
        Groups::covering((width + stride - 1) / stride, Groups::IMAGE),
        Groups::covering((height + stride - 1) / stride, Groups::IMAGE)
    );

    VKM_GL_CHECK(glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT));
    VKM_GL_CHECK(glCopyNamedBufferSubData(m_bins.getID(), readback.buffer, 0, 0, HISTOGRAM_BYTES));
    readback.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_next         = (m_next + 1) % READBACKS;
}

void GLExposure::forget() {
    dropPending();
    m_adaptation.forget();
}

void GLExposure::dropPending() {
    for (Readback& readback : m_readbacks) {
        if (!readback.fence) continue;
        VKM_GL_CHECK(glDeleteSync(readback.fence));
        readback.fence = nullptr;
    }
}

} // namespace Vkm::Engine
