#include "gl_lights.h"

#include <cstring>
#include <algorithm>

#include "logger.h"
#include "gl_uniform_buffer.h"

#include "render_view.h"

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
    LOG_TRACE("Destroying GLLights");
}

void GLLights::update(const std::vector<LightData>& lights) {
    // Prepare UBO data on stack
    LightsUBOData data;
    std::memset(&data, 0, sizeof(LightsUBOData));

    // Clamp to maximum light count
    uint32_t lightCount = std::min(static_cast<uint32_t>(lights.size()), static_cast<uint32_t>(MAX_LIGHTS));

    if (lights.size() > MAX_LIGHTS) {
        LOG_WARNING("Scene has %d lights, but only %d are supported. Excess lights will be ignored.", 
                 lights.size(), MAX_LIGHTS);
    }

    // Convert each light to GPU format
    for (uint32_t i = 0; i < lightCount; ++i) {
        const LightData& lightData = lights[i];
        LightGPUData& gpuLight = data.lights[i];

        // Position and type
        gpuLight.position = lightData.position;
        gpuLight.type = static_cast<int>(lightData.type);

        // Color and intensity
        gpuLight.color = lightData.color;
        gpuLight.intensity = lightData.intensity;

        // Direction (from rotation) and radius
        if (lightData.type == LightType::Directional || lightData.type == LightType::Spot) {
            // Compute direction from rotation (forward vector)
            gpuLight.direction = glm::normalize(lightData.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
        } else {
            // For point lights, direction is not used (set to default)
            gpuLight.direction = glm::vec3(0.0f, 0.0f, 0.0f);
        }
        gpuLight.radius = lightData.radius;

        // Spot light parameters
        gpuLight.innerConeAngle = lightData.innerConeAngle;
        gpuLight.outerConeAngle = lightData.outerConeAngle;
        gpuLight.castShadows = lightData.castShadows ? 1.0f : 0.0f;
    }

    // Set light count
    data.lightCount = static_cast<int>(lightCount);
    m_lightCount = lightCount;

    // Update or create UBO
    if (m_ubo) {
        m_ubo->update(&data, sizeof(LightsUBOData));
    } else {
        m_ubo = std::make_unique<Core::UniformBuffer>(&data, sizeof(LightsUBOData));
    }
}

void GLLights::bind(uint32_t bindingPoint) const {
    if (m_ubo) {
        m_ubo->bindBase(bindingPoint);
    }
}

} // namespace Engine
