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
 * @brief Camera controller used in the editor, supporting free-fly and look controls.
 *
 * Handles camera movement (WASD, etc.), speed boosting, mouse look, scroll zoom, and pitch/yaw.
 * Designed for use with the Editor camera Entity. Not thread-safe.
 */
class CameraController : public System {
    public:
        struct Settings {
            float zoomSensitivity  = 0.02f;     ///< Sensitivity multiplier for zooming (e.g. mouse scroll).
            float lookSensitivity  = 0.002f;    ///< Sensitivity multiplier for camera rotation (yaw/pitch).
            float moveSpeed        = 10.0f;     ///< Default movement speed (units per second).
            float speedBoost       = 3.0f;      ///< Multiplier for movement when speed boost (e.g. Shift) is active.
            float scrollMultiplier = 2.0f;      ///< Multiplier for scroll-based adjustments (zoom or movement).

            float minPitch = -90.0f;            ///< Minimum pitch angle in degrees (to prevent flipping).
            float maxPitch = 90.0f;             ///< Maximum pitch angle in degrees.
        };

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

        /// The entity the controller is currently flying (the active rendered
        /// camera). The editor uses this to suppress the transform gizmo on
        /// it - a gizmo there would fight the fly controls.
        Entity getCameraEntity() const { return m_cameraEntity; }

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

        /**
         * @brief Reads Camera, writes Transform (only the camera entity's).
         * No structural Scene changes.
         */
        SystemAccess declareAccess() const override;

        /// Writes the camera entity's Transform only; never touches
        /// ResourceManager. Safe to overlap with the render thread.
        bool mutatesResources() const override { return false; }

        Settings&       getSettings()       { return m_settings; }
        const Settings& getSettings() const { return m_settings; }
        void setSettings(const Settings& s) { m_settings = s; }
        bool isLooking() const { return m_isRightMousePressed; }

        /// Move camera to focus on a target position from a given distance.
        void focusOn(Scene& scene, const glm::vec3& target, float distance);

        /// Snap the camera to look at @p target from a specific world-space
        /// direction (e.g. (1,0,0) for "view from +X"). Used by the
        /// navigation-gizmo view presets.
        void viewFrom(Scene& scene, const glm::vec3& target,
                      const glm::vec3& direction, float distance);

    private:
        /**
         * @brief Update camera transform based on fly mode controls.
         * @param position Reference to the camera's position vector.
         * @param rotation Reference to the camera's rotation quaternion.
         * @param deltaTime Time elapsed since last update.
         */
        void updateFlyMode(WindowManager& window, glm::vec3& position, glm::quat& rotation, float deltaTime);

        /**
         * @brief Compute a quaternion from yaw/pitch and write it to @p rotation.
         *
         * Yaw first, then pitch (order matters - swapping causes roll drift).
         *
         * @param rotation Output rotation; overwritten.
         * @param yaw      Horizontal angle in radians (about world Y-up).
         * @param pitch    Vertical angle in radians (about local X-right).
         */
        void updateRotationFromAngles(glm::quat& rotation, float yaw, float pitch);

        /// Resolve the camera the editor renders through: the entity whose
        /// Camera component is `active` (Transform required). Falls back to
        /// the current entity so the view never dies mid-edit.
        EntityId resolveActiveCamera(Scene& scene);

        /// Re-derive m_yaw / m_pitch from a rotation (inverse of
        /// updateRotationFromAngles) so retargeting / focus does not snap
        /// the look direction on the next right-mouse drag.
        void reseedAnglesFromRotation(const glm::quat& rotation);

    private:
        Entity   m_cameraEntity;
        EntityId m_lastDrivenId{};   ///< Detects a camera switch -> reseed angles

        Settings m_settings;

        float m_yaw   = 0.0f;
        float m_pitch = 0.0f;
        bool m_isRightMousePressed = false;

        bool m_editorWantsMouse    = false;
        bool m_editorWantsKeyboard = false;
};

} // namespace Engine

