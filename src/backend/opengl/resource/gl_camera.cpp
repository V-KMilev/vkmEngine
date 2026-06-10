#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "resource/gl_camera.h"

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "resource/gl_ubo_util.h"
#include "system/render/data/camera_data.h"

namespace Engine {

GLCamera::GLCamera()  = default;
GLCamera::~GLCamera() = default;

void GLCamera::update(const CameraData& camera) {
    CameraUBO data;
    data.viewProjection = camera.projection * camera.view;
    data.cameraPosition = glm::vec4(camera.position, 1.0f);

    uploadUBOIfChanged(m_ubo, m_last, data);
    bindUBO(m_ubo, GLBindings::UBOBindingPoints::Camera);
}

} // namespace Engine
