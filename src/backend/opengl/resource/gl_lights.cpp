#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "resource/gl_lights.h"

#include <algorithm>

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "resource/gl_ubo_util.h"
#include "system/render/data/light_data.h"

namespace Engine {

GLLights::GLLights()  = default;
GLLights::~GLLights() = default;

void GLLights::update(const std::vector<LightData>& lights) {
    LightsUBO data{};

    const int count = std::min(static_cast<int>(lights.size()), MAX_LIGHTS);
    data.count = count;
    for (int i = 0; i < count; ++i) {
        const LightData& light = lights[i];
        GpuLight& gpu = data.lights[i];

        gpu.position  = glm::vec4(light.position, static_cast<float>(light.type));
        gpu.color     = glm::vec4(light.color, light.intensity);
        gpu.direction = glm::vec4(light.direction, light.radius);
        // shadowSlot (w) stays -1 until the shadow pass assigns casters.
        gpu.spot      = glm::vec4(light.innerConeAngle, light.outerConeAngle, 0.0f, -1.0f);
        gpu.axisU     = glm::vec4(light.axisU, light.twoSided ? 1.0f : 0.0f);
        gpu.axisV     = glm::vec4(light.axisV, 0.0f);
    }

    uploadUBOIfChanged(m_ubo, m_last, data);
    bindUBO(m_ubo, GLBindings::UBOBindingPoints::Lights);
}

} // namespace Engine
