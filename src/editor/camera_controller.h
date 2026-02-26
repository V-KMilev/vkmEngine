#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "core/system.h"

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
class CameraController : public System {
    public:
        CameraController();
        ~CameraController() override = default;

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
         * @brief Notify the controller that the editor UI is capturing input.
         *
         * When mouse capture is active, camera look/scroll is suppressed.
         * When keyboard capture is active, WASD movement is suppressed.
         * Called by EditorSystem each frame.
         */
        void setEditorInputCapture(bool mouse, bool keyboard) {
            m_editorWantsMouse    = mouse;
            m_editorWantsKeyboard = keyboard;
        }

        /**
         * @brief Update and apply camera motions (call once per-frame).
         * @param ctx The shared FrameContext for this frame.
         */
        void update(FrameContext& ctx) override;

        CameraControllerSettings& getSettings() { return m_settings; }
        const CameraControllerSettings& getSettings() const { return m_settings; }
        bool isLooking() const { return m_isRightMousePressed; }

        /// Move camera to focus on a target position from a given distance.
        void focusOn(Scene& scene, const glm::vec3& target, float distance);

    private:
        /**
         * @brief Update camera transform based on fly mode controls.
         * @param position Reference to the camera's position vector.
         * @param rotation Reference to the camera's rotation quaternion.
         * @param deltaTime Time elapsed since last update.
         */
        void updateFlyMode(WindowManager& window, glm::vec3& position, glm::quat& rotation, float deltaTime);

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

        bool m_editorWantsMouse    = false;
        bool m_editorWantsKeyboard = false;
};

} // namespace Engine

