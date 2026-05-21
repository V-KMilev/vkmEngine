#include "gl_camera.h"

#include <cstring>

#include "logger.h"

#include "gl_uniform_buffer.h"

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

    const bool firstUpload = !m_ubo;
    const bool changed = firstUpload
        || std::memcmp(&data, &m_lastData, sizeof(CameraUBOData)) != 0;

    if (!changed) return;

    if (firstUpload) {
        m_ubo = std::make_unique<Core::UniformBuffer>(
            &data, sizeof(CameraUBOData), GL_DYNAMIC_DRAW);
    } else {
        m_ubo->update(&data, sizeof(CameraUBOData));
    }
    m_lastData = data;
}

void GLCamera::bind(uint32_t bindingPoint) const {
    if (m_ubo) {
        m_ubo->bindBase(bindingPoint);
    }
}

} // namespace Engine
