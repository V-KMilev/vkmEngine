#pragma once

#include <memory>

#include <glm/glm.hpp>

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct CameraData;
}

namespace Engine {

/**
 * @brief std140 layout - must match the CameraBlock in shaders/forward.
 */
struct CameraUBO {
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;  ///< xyz = world position.
};

/**
 * @brief GPU mirror of the frame's camera - the CameraBlock UBO.
 *
 * update() packs the view camera into the std140 layout the shaders expect,
 * uploads it to the camera binding point, and skips the upload when the camera
 * is unchanged from the previous frame.
 */
class GLCamera {
    public:
        GLCamera();
        ~GLCamera();

        GLCamera(const GLCamera& other) = delete;
        GLCamera& operator=(const GLCamera& other) = delete;

        GLCamera(GLCamera && other) = delete;
        GLCamera& operator=(GLCamera && other) = delete;

    public:
        void update(const CameraData& camera);

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        CameraUBO                            m_last{};
};

} // namespace Engine
