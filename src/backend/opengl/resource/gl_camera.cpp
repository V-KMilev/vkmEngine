#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_camera.h"

#include <cstring>

#include "logger.h"

#include "gl_uniform_buffer.h"
#include "gl_ubo_util.h"
#include "system/render/render_view.h"

namespace Engine {

static_assert(sizeof(CameraUBOData) == 96, "CameraUBOData must be 96 bytes (std140 packing)");
static_assert(offsetof(CameraUBOData, cameraPosition) == 64, "cameraPosition std140 offset");
static_assert(offsetof(CameraUBOData, ambient)        == 80, "ambient std140 offset");

GLCamera::GLCamera() = default;

GLCamera::~GLCamera() {
    m_ubo.reset();
    LOG_TRACE("Destructed GLCamera");
}

void GLCamera::update(const CameraData& camera, const EnvironmentConfig& environment) {
    CameraUBOData data{};
    data.viewProjection = camera.viewProjection;
    data.cameraPosition = glm::vec4(camera.position, camera.exposure);
    data.ambient        = glm::vec4(environment.ambient.color, environment.ambient.intensity);

    uploadUBOIfChanged(m_ubo, m_lastData, data, GL_DYNAMIC_DRAW);
}

void GLCamera::bind(uint32_t bindingPoint) const {
    bindUBO(m_ubo, bindingPoint);
}

} // namespace Engine
