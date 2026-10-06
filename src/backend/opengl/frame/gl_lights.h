#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "core/engine_config.h"

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

struct LightData;

/**
 * @brief A read-only view of the per-light shadow slots GLShadowData assigned.
 *
 * Two members rather than std::span because the engine is C++17.
 */
struct LightSlots {
    const int* slots = nullptr;
    uint32_t   count = 0;

    /// @return The slot of the light at @p index, or -1 when it has none.
    int operator[](uint32_t index) const { return index < count ? slots[index] : -1; }
};

/**
 * @brief std430 layout - must match Light in shaders/lights.glsl.
 *
 * Every slot is a vec4 to avoid drivers that fail to pack a trailing scalar
 * into a vec3's 4-byte tail (spec-legal but unreliable in practice).
 *
 *  - position:  xyz = world position,  w = type (encoded as float)
 *  - color:     xyz = RGB,             w = intensity
 *  - direction: xyz = world direction, w = attenuation radius
 *  - spot:      xy = the cone as a scale and offset on cos(angle to the axis):
 *               x = 1 / (cos inner - cos outer), y = -cos outer * x;
 *               z = tan of LightData::sourceRadius (directional; 0 otherwise),
 *               w = shadowSlot (-1 = no shadow)
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

struct LightsBuffer {
    int      count;
    int      pad0, pad1, pad2;
    GpuLight lights[Config::MAX_LIGHTS];
};

/**
 * @brief GPU mirror of the frame's lights - the LightsBlock SSBO.
 *
 * update() packs each LightData into the std430 array the shaders iterate,
 * uploads it to the lights SSBO binding point, and skips the upload when the set
 * is unchanged from the previous frame. Lights past the cap are dropped. The
 * list lives in an SSBO rather than a UBO so it can grow past the UBO size
 * limit.
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
        /**
         * @brief Upload the frame's light list, tagging each with its shadow slot.
         *
         * @param lights      The frame's lights, capped at Config::MAX_LIGHTS.
         * @param shadowSlots Each light's slot in the shadow atlas; a default one
         *                    means "none of them cast".
         */
        void update(const std::vector<LightData>& lights, LightSlots shadowSlots = {});

    private:
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_ssbo;
        LightsBuffer                                 m_last{};
};

} // namespace Vkm::Engine
