#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_lights.h"

#include <algorithm>

#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"
#include "data/gl_shadow_data.h"
#include "gl_buffer_upload.h"
#include "system/render/data/light_data.h"

namespace Engine {

GLLights::GLLights()  = default;
GLLights::~GLLights() = default;

void GLLights::update(const std::vector<LightData>& lights, const GLShadowData& shadow) {
    LightsBuffer data{};

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

    // Only the header + the lights actually in use travel to the GPU; the tail
    // of the fixed-capacity array is never read (shader loops stop at count).
    const size_t activeSize = offsetof(LightsBuffer, lights) + sizeof(GpuLight) * count;
    Core::uploadPrefixIfChanged(m_ssbo, m_last, data, activeSize);
    if (m_ssbo) m_ssbo->bindBase(GLBindings::SSBOBindingPoints::Lights);
}

} // namespace Engine
