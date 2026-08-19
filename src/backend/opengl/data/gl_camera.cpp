#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_camera.h"

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "gl_buffer_upload.h"
#include "system/render/data/camera_data.h"

namespace Vkm::Engine {

GLCamera::GLCamera()  = default;
GLCamera::~GLCamera() = default;

void GLCamera::update(const CameraData& camera) {
    CameraUBO data;
    data.viewProjection = camera.viewProjection;
    data.cameraPosition = glm::vec4(camera.position, 1.0f);

    Vkm::GL::uploadIfChanged(m_ubo, m_last, data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::Camera);
}

} // namespace Vkm::Engine
