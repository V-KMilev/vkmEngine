#include "frame/gl_lights.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "gl_buffer_upload.h"
#include "gl_shader_storage_buffer.h"

#include "convention/gl_bindings.h"
#include "system/render/data/light_data.h"

namespace Vkm::Engine {

GLLights::GLLights()  = default;
GLLights::~GLLights() = default;

void GLLights::update(const std::vector<LightData>& lights, LightSlots shadowSlots) {
    LightsBuffer data{};

    const int count = static_cast<int>(std::min<size_t>(lights.size(), Config::MAX_LIGHTS));
    data.count = count;
    for (int i = 0; i < count; ++i) {
        const LightData& light = lights[i];
        GpuLight& gpu = data.lights[i];

        gpu.position  = glm::vec4(light.position, static_cast<float>(light.type));
        gpu.color     = glm::vec4(light.color, light.intensity);
        gpu.direction = glm::vec4(light.direction, light.radius);
        // w: the atlas slot this light's depth map lives in, or -1 - a
        // directional's cascade base, a spot's 2D slot, a point's cube.
        const float cosInner  = std::cos(light.innerConeAngle);
        const float cosOuter  = std::cos(light.outerConeAngle);
        const float coneScale = 1.0f / std::max(cosInner - cosOuter, glm::epsilon<float>());
        const float discTan   = light.type == LightType::Directional ? std::tan(light.sourceRadius) : 0.0f;
        gpu.spot      = glm::vec4(
            coneScale,
            -cosOuter * coneScale,
            discTan,
            static_cast<float>(shadowSlots[static_cast<uint32_t>(i)])
        );
        gpu.axisU     = glm::vec4(light.axisU, light.twoSided ? 1.0f : 0.0f);
        // w: a point or spot's emitter radius in metres, which sizes its highlight as it sizes
        // its penumbra.
        const bool sphere = light.type == LightType::Point || light.type == LightType::Spot;
        gpu.axisV     = glm::vec4(light.axisV, sphere ? light.sourceRadius : 0.0f);
    }

    // Only the header + the lights actually in use travel to the GPU; the tail
    // of the fixed-capacity array is never read (shader loops stop at count).
    const size_t activeSize = offsetof(LightsBuffer, lights) + sizeof(GpuLight) * count;
    Vkm::GL::uploadPrefixIfChanged(m_ssbo, m_last, data, activeSize);
    if (m_ssbo) m_ssbo->bindBase(GLBindings::SSBOBindingPoints::LIGHTS);
}

} // namespace Vkm::Engine
