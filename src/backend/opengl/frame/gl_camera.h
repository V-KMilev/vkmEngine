#pragma once

#include <cstddef>
#include <memory>

#include <glm/glm.hpp>

namespace Vkm::GL {
    class UniformBuffer;
}

namespace Vkm::Engine {

struct CameraData;

/**
 * @brief std140 layout - must match CameraBlock in shaders/camera.glsl.
 *
 * Everything a pass knows about the eye, so a pass need set no camera fact as a uniform of its
 * own. Ordered so std140 adds no padding; the asserts below hold it.
 */
struct CameraUBO {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::mat4 invView;
    glm::mat4 invProjection;
    glm::mat4 invViewProjection;
    glm::vec4 cameraPosition;  ///< xyz = world position.
    glm::vec2 viewport;        ///< The render target's size in pixels.
    float     zNear;
    float     zFar;
};

static_assert(offsetof(CameraUBO, cameraPosition) == 384, "CameraBlock: cameraPosition");
static_assert(offsetof(CameraUBO, viewport)       == 400, "CameraBlock: viewport");
static_assert(offsetof(CameraUBO, zNear)          == 408, "CameraBlock: zNear");
static_assert(offsetof(CameraUBO, zFar)           == 412, "CameraBlock: zFar");
static_assert(sizeof(CameraUBO)                   == 416, "CameraBlock: size");

/**
 * @brief GPU mirror of a camera - the CameraBlock UBO.
 *
 * update() skips the upload when nothing changed. Each eye drawing into a target of its own
 * owns one.
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
        /**
         * @brief Upload @p camera, drawing into a @p viewport sized target, and bind it.
         *
         * @param camera   The eye, its derived matrices already filled.
         * @param viewport Pixel size of the target the passes draw into.
         */
        void update(const CameraData& camera, const glm::vec2& viewport);

    private:
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        CameraUBO                               m_last{};
};

} // namespace Vkm::Engine
