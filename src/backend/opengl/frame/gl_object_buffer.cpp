#include "frame/gl_object_buffer.h"

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
    if (m_posed && m_skinFirst) m_skinFirst->bindBase(SSBO::INSTANCE_SKIN_BASE);
}

} // namespace Vkm::Engine
