#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_lights.h"

#include <algorithm>

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "data/gl_shadow_data.h"
#include "data/gl_ubo_util.h"
#include "system/render/data/light_data.h"

namespace Engine {

GLLights::GLLights()  = default;
GLLights::~GLLights() = default;

void GLLights::update(const std::vector<LightData>& lights, const GLShadowData& shadow) {
    LightsUBO data{};

    const int count = std::min(static_cast<int>(lights.size()), MAX_LIGHTS);
    data.count = count;
    for (int i = 0; i < count; ++i) {
        const LightData& light = lights[i];
        GpuLight& gpu = data.lights[i];

        gpu.position  = glm::vec4(light.position, static_cast<float>(light.type));
        gpu.color     = glm::vec4(light.color, light.intensity);
        gpu.direction = glm::vec4(light.direction, light.radius);
        // shadowSlot (w): the atlas slot this light's depth map lives in, or -1.
        // Directional carries its cascade base; spot a 2D slot; point a cube slot.
        gpu.spot      = glm::vec4(light.innerConeAngle, light.outerConeAngle, 0.0f,
                                  static_cast<float>(shadow.slotForLight(static_cast<uint32_t>(i))));
        gpu.axisU     = glm::vec4(light.axisU, light.twoSided ? 1.0f : 0.0f);
        gpu.axisV     = glm::vec4(light.axisV, 0.0f);
    }

    uploadUBOIfChanged(m_ubo, m_last, data);
    bindUBO(m_ubo, GLBindings::UBOBindingPoints::Lights);
}

} // namespace Engine
