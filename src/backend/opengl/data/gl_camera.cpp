#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_camera.h"

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "gl_buffer_upload.h"
#include "system/render/data/camera_data.h"

namespace Engine {

GLCamera::GLCamera()  = default;
GLCamera::~GLCamera() = default;

void GLCamera::update(const CameraData& camera) {
    CameraUBO data;
    data.viewProjection = camera.viewProjection;
    data.cameraPosition = glm::vec4(camera.position, 1.0f);

    Core::uploadIfChanged(m_ubo, m_last, data);
    Core::bindUBO(m_ubo, GLBindings::UBOBindingPoints::Camera);
}

} // namespace Engine
