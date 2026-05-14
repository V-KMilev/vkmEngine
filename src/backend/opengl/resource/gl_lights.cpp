#include "gl_lights.h"

#include <cstring>
#include <algorithm>

#include "logger.h"
#include "gl_uniform_buffer.h"

#include "system/render/render_view.h"

namespace Engine {

static_assert(sizeof(LightGPUData)  % 16 == 0, "LightGPUData must be 16-byte aligned");
static_assert(sizeof(LightGPUData) == 64, "LightGPUData must be exactly 64 bytes");
static_assert(sizeof(LightsUBOData) % 16 == 0, "LightsUBOData must be 16-byte aligned");
static_assert(offsetof(LightsUBOData, lightCount) == 0, "lightCount offset mismatch");
static_assert(offsetof(LightsUBOData, lights) == 16, "lights array offset mismatch");

GLLights::GLLights() {
}

GLLights::~GLLights() {
    m_ubo.reset();
    LOG_TRACE("Destructed GLLights");
}

void GLLights::update(const std::vector<LightData>& lights) {
    // Prepare UBO data on stack
    LightsUBOData data;
    std::memset(&data, 0, sizeof(LightsUBOData));

    // Clamp to maximum light count
    uint32_t lightCount = std::min(static_cast<uint32_t>(lights.size()), static_cast<uint32_t>(Config::MaxLights));

    if (lights.size() > Config::MaxLights) {
        LOG_WARNING("Scene has %d lights, but only %d are supported. Excess lights will be ignored.", 
                 lights.size(), Config::MaxLights);
    }

    // Convert each light to GPU format
    for (uint32_t i = 0; i < lightCount; ++i) {
        const LightData& lightData = lights[i];
        LightGPUData& gpuLight = data.lights[i];

        gpuLight.position = glm::vec4(lightData.position, static_cast<float>(lightData.type));
        gpuLight.color    = glm::vec4(lightData.color, lightData.intensity);

        const glm::vec3 dir = (lightData.type == LightType::Directional || lightData.type == LightType::Spot)
            ? glm::normalize(lightData.rotation * glm::vec3(0.0f, 0.0f, 1.0f))
            : glm::vec3(0.0f);
        gpuLight.direction = glm::vec4(dir, lightData.radius);

        gpuLight.spot = glm::vec4(
            lightData.innerConeAngle,
            lightData.outerConeAngle,
            0.0f,
            static_cast<float>(lightData.shadowSlot)  // -1 = no shadow
        );
    }

    data.lightCount = static_cast<int>(lightCount);
    m_lightCount = lightCount;

    const bool firstUpload = !m_ubo;
    const bool changed = firstUpload
        || std::memcmp(&data, &m_lastData, sizeof(LightsUBOData)) != 0;

    if (!changed) return;

    if (firstUpload) {
        m_ubo = std::make_unique<Core::UniformBuffer>(&data, sizeof(LightsUBOData));
    } else {
        m_ubo->update(&data, sizeof(LightsUBOData));
    }
    m_lastData = data;
}

void GLLights::bind(uint32_t bindingPoint) const {
    if (m_ubo) {
        m_ubo->bindBase(bindingPoint);
    }
}

} // namespace Engine
