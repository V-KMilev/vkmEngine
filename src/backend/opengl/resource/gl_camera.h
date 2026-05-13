#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "config/gl_config.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct CameraData;
    struct EnvironmentConfig;
}

namespace Engine {

/**
 * @brief Per-frame camera/environment UBO data, std140 layout.
 *
 * Uses vec4 for every vec3+scalar pair to avoid drivers that fail to pack a
 * trailing float into a vec3's 4-byte tail (spec-legal but unreliable in
 * practice).
 *
 * Slot layout:
 *  - viewProjection: 64 bytes
 *  - cameraPosition: xyz = world position, w = exposure
 *  - ambient:        xyz = ambient color,  w = ambient intensity
 */
struct alignas(16) CameraUBOData {
    glm::mat4 viewProjection;   // offset 0,  64 bytes
    glm::vec4 cameraPosition;   // offset 64, 16 bytes
    glm::vec4 ambient;          // offset 80, 16 bytes
};

/**
 * @brief Encapsulates the per-frame camera UBO upload and binding.
 *
 * Mirrors GLLights' shape: holds a Core::UniformBuffer, exposes update()
 * and bind(). Skips the GPU upload when the data is byte-identical to the
 * previous frame (memcmp on the POD UBO struct).
 */
class GLCamera {
    public:
        GLCamera();
        ~GLCamera();

        GLCamera(const GLCamera& other) = delete;
        GLCamera& operator=(const GLCamera& other) = delete;

        GLCamera(GLCamera&& other) = delete;
        GLCamera& operator=(GLCamera&& other) = delete;

    public:
        /**
         * @brief Build the per-frame UBO data and upload if it changed.
         */
        void update(const CameraData& camera, const EnvironmentConfig& environment);

        /**
         * @brief Bind the camera UBO to the specified binding point.
         */
        void bind(uint32_t bindingPoint = GLConfig::UBOBindingPoints::Camera) const;

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        CameraUBOData                        m_lastData{};
};

} // namespace Engine
