#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @struct CameraProperties
 * @brief Encapsulates basic camera parameters for view/projection matrix computation.
 *
 * Members:
 *   - position:      Position of the camera in world space.
 *   - target:        Point in world space the camera is looking at.
 *   - up:            Up direction for the camera (used for orientation).
 *   - fovYDegrees:   Field of view in the Y direction, in degrees.
 *   - aspect:        Aspect ratio of the camera's viewport (width / height).
 *   - nearPlane:     Near clipping plane distance.
 *   - farPlane:      Far clipping plane distance.
 */
struct CameraProperties {
    glm::vec3 position {0.0f, 0.0f, 3.0f};
    glm::vec3 target   {0.0f, 0.0f, 0.0f};
    glm::vec3 up       {0.0f, 1.0f, 0.0f};
    float fovYDegrees  {60.0f};
    float aspect       {16.0f / 9.0f};
    float nearPlane    {0.001f};
    float farPlane     {100.0f};
};

/**
 * @class CPUCamera
 * @brief Represents a camera on the CPU side, handling view and projection matrix calculations.
 *
 * The CPUCamera manages camera position, orientation, and projection state, and
 * automatically updates relevant view and projection matrices when parameters change.
 * Provides methods to manipulate camera properties and retrieve current camera matrices.
 * Copy and move operations are disabled to maintain resource safety.
 */
class CPUCamera {
    public:
        CPUCamera();
        ~CPUCamera() = default;

        CPUCamera(const CPUCamera& other) = delete;
        CPUCamera& operator=(const CPUCamera& other) = delete;

        CPUCamera(CPUCamera && other) = delete;
        CPUCamera& operator=(CPUCamera && other) = delete;

        explicit CPUCamera(const CameraProperties& properties);

    public:
        /**
         * @brief Sets the camera's world-space position.
         * @param position New camera position.
         */
        void setPosition(const glm::vec3& position);

        /**
         * @brief Sets the camera's target/look-at point.
         * @param target New target position.
         */
        void setTarget(const glm::vec3& target);

        /**
         * @brief Sets the camera's up direction.
         * @param up New up vector.
         */
        void setUp(const glm::vec3& up);

        /**
         * @brief Sets perspective projection parameters.
         * @param fovYDegrees Field of view in Y direction (degrees).
         * @param aspect Aspect ratio of the camera.
         * @param nearPlane Near plane distance.
         * @param farPlane Far plane distance.
         */
        void setPerspective(float fovYDegrees, float aspect, float nearPlane, float farPlane);

        /**
         * @brief Sets only the aspect ratio.
         * @param aspect New aspect ratio.
         */
        void setAspect(float aspect);

        /**
         * @brief Gets the current camera position.
         * @return Reference to camera position.
         */
        const glm::vec3& getPosition() const { return m_properties.position; }

        /**
         * @brief Gets the current camera target.
         * @return Reference to camera target.
         */
        const glm::vec3& getTarget() const { return m_properties.target; }

        /**
         * @brief Gets the current up vector.
         * @return Reference to up vector.
         */
        const glm::vec3& getUp() const { return m_properties.up; }

        /**
         * @brief Gets the current view matrix.
         * @return Reference to view (look-at) matrix.
         */
        const glm::mat4& getViewMatrix() const { return m_view; }

        /**
         * @brief Gets the current projection matrix.
         * @return Reference to projection matrix.
         */
        const glm::mat4& getProjectionMatrix() const { return m_projection; }

        /**
         * @brief Gets the combined view-projection matrix.
         * @return Reference to view-projection matrix.
         */
        const glm::mat4& getViewProjectionMatrix() const { return m_viewProjection; }

    private:
        /**
         * @brief Updates the view matrix based on position, target, and up vector.
         */
        void updateView();

        /**
         * @brief Updates the projection matrix based on perspective settings.
         */
        void updateProjection();

    private:
        CameraProperties m_properties;

        glm::mat4 m_view;
        glm::mat4 m_projection;
        glm::mat4 m_viewProjection;
};

} // namespace Engine

