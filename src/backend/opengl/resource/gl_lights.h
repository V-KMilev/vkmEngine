#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "config/gl_config.h"
#include "core/engine_config.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct LightData;
}

namespace Engine {

/**
 * @brief Light data structure matching GLSL std140 layout for uniform buffer.
 *
 * Each light occupies exactly 96 bytes. Uses vec4 for every slot to avoid
 * drivers that fail to pack a trailing scalar into a vec3's 4-byte tail
 * (spec-legal but unreliable in practice).
 *
 * Slot layout:
 *  - position:   xyz = world position,  w = type (encoded as float, cast in shader)
 *  - color:      xyz = RGB,             w = intensity
 *  - direction:  xyz = world direction (forward / surface normal),
 *                w   = attenuation radius
 *  - spot:       x   = inner cone,      y = outer cone, z = unused, w = shadowSlot (-1 = no shadow)
 *  - axisU:      xyz = half-right world vector (rotation * +X * width/2 for Rect, * areaRadius for Disk),
 *                w   = twoSided (0 or 1, only meaningful for Rect/Disk)
 *  - axisV:      xyz = half-up world vector (rotation * +Y * height/2 for Rect, * areaRadius for Disk),
 *                w   = unused
 *
 * For non-area lights (Directional / Point / Spot) axisU/axisV are zero; the
 * shader's area-light branches are gated on `type`.
 */
struct alignas(16) LightGPUData {
    glm::vec4 position;          // offset 0,  16 bytes  (xyz=position, w=type)
    glm::vec4 color;             // offset 16, 16 bytes  (xyz=color,    w=intensity)
    glm::vec4 direction;         // offset 32, 16 bytes  (xyz=dir,      w=radius)
    glm::vec4 spot;              // offset 48, 16 bytes  (x=inner, y=outer, z=unused, w=shadowSlot)
    glm::vec4 axisU;             // offset 64, 16 bytes  (xyz=half-right, w=twoSided)
    glm::vec4 axisV;             // offset 80, 16 bytes  (xyz=half-up,    w=unused)
};

/**
 * @brief Lights uniform block data matching GLSL std140 layout.
 *
 * Contains an array of light data and a count of active lights.
 * Must match the LightsBlock uniform block in the shader exactly.
 *
 * Layout:
 * - lightCount: offset 0, 4 bytes (12 bytes padding to next array element)
 * - lights:     offset 16, 96 * Config::MaxLights bytes
 *
 * Total size: 16 + (96 * Config::MaxLights) = 16 + (96 * 32) = 16 + 3072 = 3088 bytes
 */
struct alignas(16) LightsUBOData {
    int lightCount;                              // offset 0, 4 bytes
    char _padding[12];                           // offset 4, 12 bytes (explicit padding to offset 16)
    LightGPUData lights[Config::MaxLights];      // offset 16, 96 bytes per light
};

/**
 * @brief Encapsulates OpenGL light management, maintaining a uniform buffer for all scene lights.
 *
 * GLLights collects light data from the scene and uploads it to a single UBO for efficient
 * GPU access. It supports directional, point, and spot lights with proper attenuation and culling.
 * 
 * Usage:
 * 1. Call update() with the RenderView to collect and upload all lights
 * 2. Call bind() before rendering
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
         * @brief Update the light buffer from RenderView.
         * 
         * Collects all lights from the render view, converts them to GPU format,
         * and uploads them to the UBO.
         * 
         * @param lights Vector of light data from the RenderView.
         */
        void update(const std::vector<LightData>& lights);

        /**
         * @brief Bind the lights uniform buffer.
         *
         * @param bindingPoint The UBO binding point index.
         */
        void bind(uint32_t bindingPoint = GLConfig::UBOBindingPoints::Lights) const;

        /**
         * @brief Get the current number of lights in the buffer.
         */
        uint32_t getLightCount() const { return m_lightCount; }

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        LightsUBOData m_lastData{};
        uint32_t m_lightCount = 0;
};

} // namespace Engine
