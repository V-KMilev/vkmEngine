#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct LightData;
}

namespace Engine {

static constexpr int MAX_LIGHTS = 16;

/**
 * @brief std140 layout - must match the Light struct in shaders/forward.
 *
 * Every slot is a vec4 to avoid drivers that fail to pack a trailing scalar
 * into a vec3's 4-byte tail (spec-legal but unreliable in practice).
 *
 *  - position:  xyz = world position,  w = type (encoded as float)
 *  - color:     xyz = RGB,             w = intensity
 *  - direction: xyz = world direction, w = attenuation radius
 *  - spot:      x = inner cone, y = outer cone (radians),
 *               z = unused, w = shadowSlot (-1 = no shadow)
 *  - axisU:     xyz = half-right world axis (Rect/Disk), w = twoSided (0/1)
 *  - axisV:     xyz = half-up    world axis (Rect/Disk), w = unused
 *
 * For punctual lights (Directional / Point / Spot) axisU/axisV are zero; the
 * shader's area-light branch is gated on type.
 */
struct GpuLight {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 direction;
    glm::vec4 spot;
    glm::vec4 axisU;
    glm::vec4 axisV;
};

struct LightsUBO {
    int      count;
    int      pad0, pad1, pad2;
    GpuLight lights[MAX_LIGHTS];
};

/**
 * @brief GPU mirror of the frame's lights - the LightsBlock UBO.
 *
 * update() packs each LightData into the std140 array the shaders iterate,
 * uploads it to the lights binding point, and skips the upload when the set is
 * unchanged from the previous frame. Lights past the cap are dropped.
 */
class GLLights {
    public:
        GLLights();
        ~GLLights();

        GLLights(const GLLights& other) = delete;
        GLLights& operator=(const GLLights& other) = delete;

        GLLights(GLLights && other) = delete;
        GLLights& operator=(GLLights && other) = delete;

    public:
        void update(const std::vector<LightData>& lights);

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        LightsUBO                            m_last{};
};

} // namespace Engine
