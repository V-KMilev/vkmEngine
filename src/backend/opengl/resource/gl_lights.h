#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "config/gl_config.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct LightData;
}

namespace Engine {

// Maximum number of lights supported in a single draw call
constexpr uint32_t MAX_LIGHTS = GLConfig::Limits::MaxLights;

/**
 * @brief Light data structure matching GLSL std140 layout for uniform buffer.
 *
 * Each light occupies exactly 64 bytes. Uses vec4 for every slot to avoid
 * drivers that fail to pack a trailing scalar into a vec3's 4-byte tail
 * (spec-legal but unreliable in practice).
 *
 * Slot layout:
 *  - position:   xyz = world position,  w = type (encoded as float, cast in shader)
 *  - color:      xyz = RGB,             w = intensity
 *  - direction:  xyz = world direction, w = radius
 *  - spot:       x   = inner cone,      y = outer cone, z = castShadows, w = pad
 *
 * Note: Disabled lights are filtered out when building RenderView.
 */
struct alignas(16) LightGPUData {
    glm::vec4 position;          // offset 0,  16 bytes  (xyz=position, w=type)
    glm::vec4 color;             // offset 16, 16 bytes  (xyz=color,    w=intensity)
    glm::vec4 direction;         // offset 32, 16 bytes  (xyz=dir,      w=radius)
    glm::vec4 spot;              // offset 48, 16 bytes  (x=inner, y=outer, z=castShadows)
};

/**
 * @brief Lights uniform block data matching GLSL std140 layout.
 * 
 * Contains an array of light data and a count of active lights.
 * Must match the LightsBlock uniform block in the shader exactly.
 * 
 * Layout:
 * - lightCount: offset 0, 4 bytes (12 bytes padding to next array element)
 * - lights:     offset 16, 64 * GLConfig::Limits::MaxLights bytes
 * 
 * Total size: 16 + (64 * GLConfig::Limits::MaxLights) = 16 + (64 * 32) = 16 + 2048 = 2064 bytes
 */
struct alignas(16) LightsUBOData {
    int lightCount;                                    // offset 0, 4 bytes
    char _padding[12];                                 // offset 4, 12 bytes (explicit padding to offset 16)
    LightGPUData lights[GLConfig::Limits::MaxLights];  // offset 16, 64 bytes per light
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

        GLLights(GLLights&& other) = delete;
        GLLights& operator=(GLLights&& other) = delete;

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
