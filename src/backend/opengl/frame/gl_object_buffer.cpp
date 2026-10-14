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
    const auto count = static_cast<uint32_t>(objects.models.size());

    // Runs of consecutive changed objects, each patched into the video-memory copies in place.
    m_runs.clear();
    for (const uint32_t object : objects.changed) {
        if (!m_runs.empty() && m_runs.back().second == object) ++m_runs.back().second;
        else m_runs.emplace_back(object, object + 1);
    }
    const bool whole = objects.allChanged || objects.serial != m_serial + 1 || posed != m_posed
        || m_runs.size() > MAX_PATCH_RUNS;
    const bool resized = count != m_count;
    m_serial = objects.serial;
    m_count  = count;
    m_posed  = posed;

    // Never empty, so the flags always exist to be bound.
    m_flagScratch.resize(std::max<size_t>(objects.draws.size(), 1), 0u);
    const auto flagOf = [&](uint32_t object) {
        return objects.draws[object].viewmodel ? INSTANCE_FLAG_VIEWMODEL : 0u;
    };

    if (whole) {
        const auto bytes = [](const auto& list) {
            return static_cast<uint32_t>(list.size() * sizeof(list[0]));
        };
        for (uint32_t i = 0; i < count; ++i) m_flagScratch[i] = flagOf(i);
        m_modelStream.upload(stream, objects.models.data(), bytes(objects.models));
        m_flagStream.upload(stream, m_flagScratch.data(), bytes(m_flagScratch));
        if (posed) m_skinFirstStream.upload(stream, objects.skinFirst.data(), bytes(objects.skinFirst));
        return;
    }

    const auto word   = static_cast<uint32_t>(sizeof(uint32_t));
    const auto matrix = static_cast<uint32_t>(sizeof(glm::mat4));
    const auto box    = static_cast<uint32_t>(sizeof(Math::AABB));

    // Grown or shrunk by a spawn or a removal: what the copies hold stays, and an index new to them
    // is among the changed.
    if (resized) {
        m_modelStream.resize(count * matrix);
        m_flagStream.resize(count * word);
        if (posed) m_skinFirstStream.resize(count * word);
    }
    for (const auto& [first, end] : m_runs) {
        const uint32_t n = end - first;
        for (uint32_t i = first; i < end; ++i) m_flagScratch[i] = flagOf(i);
        m_modelStream.patch(stream, first * matrix, &objects.models[first], n * matrix);
        m_flagStream.patch(stream, first * word, &m_flagScratch[first], n * word);
        if (posed) m_skinFirstStream.patch(stream, first * word, &objects.skinFirst[first], n * word);
    }
}

void GLObjectBuffer::bind() const {
    namespace SSBO = GLBindings::SSBOBindingPoints;
    const auto bind = [](uint32_t point, const Vkm::GL::BufferSpan& span) {
        if (span) Vkm::GL::bindRange(GL_SHADER_STORAGE_BUFFER, point, span);
    };
    bind(SSBO::INSTANCE_MODELS, m_modelStream.span());
    bind(SSBO::INSTANCE_FLAGS, m_flagStream.span());
    if (m_posed) bind(SSBO::INSTANCE_SKIN_BASE, m_skinFirstStream.span());
}

} // namespace Vkm::Engine
