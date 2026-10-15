#include "frame/gl_camera.h"

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "gl_buffer_upload.h"
#include "system/render/data/camera_data.h"

namespace Vkm::Engine {

GLCamera::GLCamera()  = default;
GLCamera::~GLCamera() = default;

void GLCamera::update(const CameraData& camera, const glm::vec2& viewport) {
    // The engine's matrices with the eye's reversed depth (gl_depth.h); inverted in double, as
    // the reversed z row is small beside the others.
    CameraUBO data;
    data.view              = camera.view;
    data.projection        = GLDepth::eyeProjection(camera.projection);
    data.viewProjection    = GLDepth::eyeProjection(camera.viewProjection);
    data.invView           = camera.invView;
    data.invProjection     = glm::mat4(glm::inverse(glm::dmat4(data.projection)));
    data.invViewProjection = glm::mat4(glm::inverse(glm::dmat4(data.viewProjection)));
    data.cameraPosition    = glm::vec4(camera.position, 1.0f);
    data.viewport          = viewport;
    data.zNear             = camera.zNear;
    data.zFar              = camera.zFar;
    data.viewmodelViewProjection = GLDepth::eyeProjection(camera.viewmodelViewProjection);

    Vkm::GL::uploadIfChanged(m_ubo, m_last, data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::CAMERA);
}

} // namespace Vkm::Engine
