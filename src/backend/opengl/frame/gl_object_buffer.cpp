#include "frame/gl_object_buffer.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "gl_stream_ring.h"

#include "convention/gl_bindings.h"
#include "debug/profiler.h"
#include "system/render/data/render_objects.h"

namespace Vkm::Engine {

GLObjectBuffer::GLObjectBuffer()  = default;
GLObjectBuffer::~GLObjectBuffer() = default;

void GLObjectBuffer::upload(const RenderObjects& objects, bool posed, Vkm::GL::StreamRing& stream) {
    PROFILE_SCOPE("Objects/Upload");
    const auto bytes = [](const auto& list) { return static_cast<uint32_t>(list.size() * sizeof(list[0])); };
    m_models = m_modelStream.upload(stream, objects.models.data(), bytes(objects.models));

    // Never empty, so the buffer always exists to be bound: a shader reading it reads zeros.
    m_flagScratch.assign(std::max<size_t>(objects.draws.size(), 1), 0u);
    for (size_t i = 0; i < objects.draws.size(); ++i) {
        if (objects.draws[i].viewmodel) m_flagScratch[i] = INSTANCE_FLAG_VIEWMODEL;
    }
    m_flags = m_flagStream.upload(stream, m_flagScratch.data(), bytes(m_flagScratch));

    m_posed = posed;
    if (posed) {
        m_skinFirst = m_skinFirstStream.upload(stream, objects.skinFirst.data(), bytes(objects.skinFirst));
    }
}

void GLObjectBuffer::bind() const {
    namespace SSBO = GLBindings::SSBOBindingPoints;
    const auto bind = [](uint32_t point, const Vkm::GL::BufferSpan& span) {
        if (span) Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, point, span);
    };
    bind(SSBO::INSTANCE_MODELS, m_models);
    bind(SSBO::INSTANCE_FLAGS, m_flags);
    if (m_posed) bind(SSBO::INSTANCE_SKIN_BASE, m_skinFirst);
}

} // namespace Vkm::Engine
