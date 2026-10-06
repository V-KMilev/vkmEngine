#include "frame/gl_camera.h"

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "gl_buffer_upload.h"
#include "system/render/data/camera_data.h"

namespace Vkm::Engine {

GLCamera::GLCamera()  = default;
GLCamera::~GLCamera() = default;

void GLCamera::update(const CameraData& camera, const glm::vec2& viewport) {
    CameraUBO data;
    data.view              = camera.view;
    data.projection        = camera.projection;
    data.viewProjection    = camera.viewProjection;
    data.invView           = camera.invView;
    data.invProjection     = camera.invProjection;
    data.invViewProjection = camera.invViewProj;
    data.cameraPosition    = glm::vec4(camera.position, 1.0f);
    data.viewport          = viewport;
    data.zNear             = camera.zNear;
    data.zFar              = camera.zFar;

    Vkm::GL::uploadIfChanged(m_ubo, m_last, data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::CAMERA);
}

} // namespace Vkm::Engine
