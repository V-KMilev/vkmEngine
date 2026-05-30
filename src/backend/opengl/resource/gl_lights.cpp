#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_lights.h"

#include <algorithm>
#include <cstring>

#include "logger.h"

#include "gl_uniform_buffer.h"
#include "gl_ubo_util.h"
#include "system/render/render_view.h"

namespace Engine {

static_assert(sizeof(LightGPUData)  % 16 == 0, "LightGPUData must be 16-byte aligned");
static_assert(sizeof(LightGPUData) == 96, "LightGPUData must be exactly 96 bytes");
static_assert(sizeof(LightsUBOData) % 16 == 0, "LightsUBOData must be 16-byte aligned");
static_assert(offsetof(LightsUBOData, lightCount) == 0, "lightCount offset mismatch");
static_assert(offsetof(LightsUBOData, lights) == 16, "lights array offset mismatch");

GLLights::GLLights() = default;

GLLights::~GLLights() {
    m_ubo.reset();
    LOG_TRACE("Destructed GLLights");
}

void GLLights::update(const std::vector<LightData>& lights) {
    // Prepare UBO data on stack
    LightsUBOData data;
    std::memset(&data, 0, sizeof(LightsUBOData));

    // Clamp to maximum light count
    uint32_t lightCount = std::min(static_cast<uint32_t>(lights.size()), static_cast<uint32_t>(Config::MAX_LIGHTS));

    if (lights.size() > Config::MAX_LIGHTS) {
        LOG_WARNING("Scene has %d lights, but only %d are supported. Excess lights will be ignored.", 
                 lights.size(), Config::MAX_LIGHTS);
    }

    // Convert each light to GPU format
    for (uint32_t i = 0; i < lightCount; ++i) {
        const LightData& lightData = lights[i];
        LightGPUData& gpuLight = data.lights[i];

        gpuLight.position = glm::vec4(lightData.position, static_cast<float>(lightData.type));
        gpuLight.color    = glm::vec4(lightData.color, lightData.intensity);

        // Directional, Spot, and area lights (Rect/Disk) all need a world-
        // space direction. Point lights leave it zeroed.
        const bool hasDirection =
               lightData.type == LightType::Directional
            || lightData.type == LightType::Spot
            || lightData.type == LightType::Rect
            || lightData.type == LightType::Disk;
        const glm::vec3 dir = hasDirection
            ? glm::normalize(lightData.rotation * glm::vec3(0.0f, 0.0f, 1.0f))
            : glm::vec3(0.0f);
        gpuLight.direction = glm::vec4(dir, lightData.radius);

        gpuLight.spot = glm::vec4(
            lightData.innerConeAngle,
            lightData.outerConeAngle,
            0.0f,
            static_cast<float>(lightData.shadowSlot)  // -1 = no shadow
        );

        // Area-light geometry: half-extent world-space axes so the shader can
        // recover the rectangle's four corners as
        //   p_i = position +/- axisU.xyz +/- axisV.xyz
        // Disk shares the same encoding (axisU and axisV both have magnitude
        // areaRadius); the shader treats the disk as a 4-vertex polygon
        // inscribed in the disk.
        glm::vec3 halfRight(0.0f);
        glm::vec3 halfUp(0.0f);
        if (lightData.type == LightType::Rect) {
            const glm::vec3 right = lightData.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 up    = lightData.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            halfRight = right * (lightData.areaWidth  * 0.5f);
            halfUp    = up    * (lightData.areaHeight * 0.5f);
        } else if (lightData.type == LightType::Disk) {
            const glm::vec3 right = lightData.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 up    = lightData.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            halfRight = right * lightData.areaRadius;
            halfUp    = up    * lightData.areaRadius;
        }
        gpuLight.axisU = glm::vec4(halfRight, lightData.twoSided ? 1.0f : 0.0f);
        gpuLight.axisV = glm::vec4(halfUp,    0.0f);
    }

    data.lightCount = static_cast<int>(lightCount);
    m_lightCount = lightCount;

    // GL_STATIC_DRAW preserves the historical ctor default (this UBO is
    // rewritten per frame, so DYNAMIC would arguably fit better - left as-is
    // to keep this a pure dedup).
    uploadUBOIfChanged(m_ubo, m_lastData, data, GL_STATIC_DRAW);
}

void GLLights::bind(uint32_t bindingPoint) const {
    bindUBO(m_ubo, bindingPoint);
}

} // namespace Engine
