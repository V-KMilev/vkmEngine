#include "frame/gl_object_buffer.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"
#include "frame/gl_stream_upload.h"
#include "debug/profiler.h"
#include "system/render/data/render_objects.h"

namespace Vkm::Engine {

GLObjectBuffer::GLObjectBuffer()  = default;
GLObjectBuffer::~GLObjectBuffer() = default;

void GLObjectBuffer::upload(const RenderObjects& objects, bool posed) {
    PROFILE_SCOPE("Objects/Upload");
    growAndUpload(
        m_models,
        m_modelCapacity,
        objects.models.data(),
        static_cast<uint32_t>(objects.models.size() * sizeof(glm::mat4))
    );

    // Never empty, so the buffer always exists to be bound: a shader reading it reads zeros.
    m_flagScratch.assign(std::max<size_t>(objects.draws.size(), 1), 0u);
    for (size_t i = 0; i < objects.draws.size(); ++i) {
        if (objects.draws[i].viewmodel) m_flagScratch[i] = INSTANCE_FLAG_VIEWMODEL;
    }
    growAndUpload(
        m_flags,
        m_flagCapacity,
        m_flagScratch.data(),
        static_cast<uint32_t>(m_flagScratch.size() * sizeof(uint32_t))
    );

    m_posed = posed;
    if (posed) {
        growAndUpload(
            m_skinFirst,
            m_skinFirstCapacity,
            objects.skinFirst.data(),
            static_cast<uint32_t>(objects.skinFirst.size() * sizeof(uint32_t))
        );
    }
}

void GLObjectBuffer::bind() const {
    namespace SSBO = GLBindings::SSBOBindingPoints;
    if (m_models) m_models->bindBase(SSBO::INSTANCE_MODELS);
    if (m_flags) m_flags->bindBase(SSBO::INSTANCE_FLAGS);
    if (m_posed && m_skinFirst) m_skinFirst->bindBase(SSBO::INSTANCE_SKIN_BASE);
}

} // namespace Vkm::Engine
