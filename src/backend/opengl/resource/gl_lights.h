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
 * Each light occupies exactly 64 bytes for efficient GPU access.
 * Uses proper types for better readability while maintaining std140 alignment.
 * 
 * std140 alignment rules:
 * - vec3: 16-byte alignment, 12-byte size
 * - float/int: 4-byte alignment, 4-byte size
 * - Scalars can pack after vec3 within the same 16-byte block
 * 
 * Note: Disabled lights are filtered out when building RenderView, so no 'enabled' field needed.
 */
struct alignas(16) LightGPUData {
    glm::vec3 position;          // offset 0,  12 bytes
    int type;                    // offset 12, 4 bytes (0=directional, 1=point, 2=spot)

    glm::vec3 color;             // offset 16, 12 bytes
    float intensity;             // offset 28, 4 bytes

    glm::vec3 direction;         // offset 32, 12 bytes
    float radius;                // offset 44, 4 bytes

    float innerConeAngle;        // offset 48, 4 bytes
    float outerConeAngle;        // offset 52, 4 bytes
    float castShadows;           // offset 56, 4 bytes (0.0 = false, 1.0 = true)
    float _padding;              // offset 60, 4 bytes - REQUIRED for std140 array stride of 64 bytes
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
         * Binds the lights UBO to the specified binding point.
         * 
         * @param bindingPoint The UBO binding point index (default: 1).
         */
        void bind(uint32_t bindingPoint = 1) const;

        /**
         * @brief Get the current number of lights in the buffer.
         */
        uint32_t getLightCount() const { return m_lightCount; }

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        uint32_t m_lightCount;
};

} // namespace Engine
