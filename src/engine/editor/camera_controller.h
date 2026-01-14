#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>
#include <algorithm>

#include "entity.h"
#include "scene.h"

namespace Engine {

/**
 * @brief Stores configurable properties that control camera behavior in the editor.
 */
struct CameraControllerSettings {
    float zoomSensitivity  = 0.02f;     ///< Sensitivity multiplier for zooming (e.g. mouse scroll).
    float lookSensitivity  = 0.002f;    ///< Sensitivity multiplier for camera rotation (yaw/pitch).
    float moveSpeed        = 10.0f;     ///< Default movement speed (units per second).
    float speedBoost       = 3.0f;      ///< Multiplier for movement when speed boost (e.g. Shift) is active.
    float scrollMultiplier = 2.0f;      ///< Multiplier for scroll-based adjustments (zoom or movement).

    float minPitch = -90.0f;            ///< Minimum pitch angle in degrees (to prevent flipping).
    float maxPitch = 90.0f;             ///< Maximum pitch angle in degrees.
};

/**
 * @brief Camera controller used in the editor, supporting free-fly and look controls.
 *
 * Handles camera movement (WASD, etc.), speed boosting, mouse look, scroll zoom, and pitch/yaw.
 * Designed for use with the Editor camera Entity. Not thread-safe.
 */
class CameraController {
    public:
        CameraController();
        ~CameraController() = default;

        CameraController(const CameraController& other) = delete;
        CameraController& operator=(const CameraController& other) = delete;

        CameraController(CameraController && other) = delete;
        CameraController& operator=(CameraController && other) = delete;

    public:
        /**
         * @brief Set the Entity ID of the camera to be controlled.
         * @param cameraEntity The entity representing the camera.
         */
        void setCameraEntity(Entity cameraEntity) { m_cameraEntity = cameraEntity; }

        /**
         * @brief Update and apply camera motions (call once per-frame).
         * @param scene The ECS scene.
         * @param deltaTime Time elapsed since last update.
         */
        void update(Scene& scene, float deltaTime);

    private:
        /**
         * @brief Update camera transform based on fly mode controls.
         * @param position Reference to the camera's position vector.
         * @param rotation Reference to the camera's rotation quaternion.
         * @param deltaTime Time elapsed since last update.
         */
        void updateFlyMode(glm::vec3& position, glm::quat& rotation, float deltaTime);

        /**
         * @brief Compute a quaternion from yaw/pitch angles and apply to rotation.
         * @param rotation Reference to output rotation.
         * @param yaw Horizontal angle (radians or degrees as internally used).
         * @param pitch Vertical angle.
         */
        void updateRotationFromAngles(glm::quat& rotation, float yaw, float pitch);

    private:
        Entity m_cameraEntity;

        CameraControllerSettings m_settings;

        float m_yaw   = 0.0f;
        float m_pitch = 0.0f;
        bool m_isRightMousePressed = false;
};

} // namespace Engine

