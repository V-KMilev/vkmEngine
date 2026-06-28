#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "core/system.h"

namespace Engine {

struct Transform;

/**
 * @brief Camera controller used in the editor, supporting free-fly and look controls.
 *
 * Handles camera movement (WASD, etc.), speed boosting, mouse look, scroll zoom, and pitch/yaw.
 * Designed for use with the Editor camera Entity. Not thread-safe.
 */
class CameraControllerSystem : public System {
    public:
        /**
         * @brief Tunable feel parameters for movement, look, and zoom.
         */
        struct Settings {
            float zoomSensitivity  = 0.02f;     ///< Sensitivity multiplier for zooming (e.g. mouse scroll).
            float lookSensitivity  = 0.002f;    ///< Sensitivity multiplier for camera rotation (yaw/pitch).
            float moveSpeed        = 10.0f;     ///< Default movement speed (units per second).
            float speedBoost       = 3.0f;      ///< Multiplier for movement when speed boost (e.g. Shift) is active.
            float scrollMultiplier = 2.0f;      ///< Multiplier for scroll-based forward/back dolly.

            float minPitch = -90.0f;            ///< Minimum pitch angle in degrees (to prevent flipping).
            float maxPitch = 90.0f;             ///< Maximum pitch angle in degrees.
        };

        CameraControllerSystem();
        ~CameraControllerSystem() override = default;

        CameraControllerSystem(const CameraControllerSystem& other) = delete;
        CameraControllerSystem& operator=(const CameraControllerSystem& other) = delete;

        CameraControllerSystem(CameraControllerSystem && other) = delete;
        CameraControllerSystem& operator=(CameraControllerSystem && other) = delete;

    public:
        /**
         * @brief Set the Entity ID of the camera to be controlled.
         * @param cameraEntity The entity representing the camera.
         */
        void setCameraEntity(Entity cameraEntity) { m_cameraEntity = cameraEntity; }

        /**
         * @brief The entity the controller is currently flying (the active rendered
         * camera). The editor uses this to suppress the transform gizmo on
         * it - a gizmo there would fight the fly controls.
         */
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
         * @brief Resolve the active rendered camera and apply fly-mode motion.
         *
         * Each frame this retargets onto whichever entity has an active Camera
         * component (so "you move what you see"), reseeding yaw/pitch on a
         * camera switch, then applies look/move/zoom. Call once per frame.
         * @param ctx The shared FrameContext for this frame.
         */
        void update(FrameContext& ctx) override;

        Settings&       getSettings()       { return m_settings; }
        const Settings& getSettings() const { return m_settings; }
        void setSettings(const Settings& s) { m_settings = s; }
        bool isLooking() const { return m_isRightMousePressed; }

        /**
         * @brief Move the camera to focus on a target position from a given distance.
         *
         * @param scene    The scene whose camera transform is updated.
         * @param target   World-space point the camera should center on.
         * @param distance Distance to pull back from @p target along the view direction.
         */
        void focusOn(Scene& scene, const glm::vec3& target, float distance);

        /**
         * @brief Snap the camera to look at @p target from a specific world-space
         * direction (e.g. (1,0,0) for "view from +X"). Used by the
         * navigation-gizmo view presets.
         */
        void viewFrom(Scene& scene, const glm::vec3& target, const glm::vec3& direction, float distance);

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

        /**
         * @brief Resolve the camera the editor renders through: the entity whose
         * Camera component is `active` (Transform required). Falls back to
         * the current entity so the view never dies mid-edit.
         */
        EntityId resolveActiveCamera(Scene& scene);

        /**
         * @brief Re-derive m_yaw / m_pitch from a rotation (inverse of
         * updateRotationFromAngles) so retargeting / focus does not snap
         * the look direction on the next right-mouse drag.
         */
        void reseedAnglesFromRotation(const glm::quat& rotation);

        /**
         * @brief Set m_yaw / m_pitch from a (normalized) look direction, the
         * inverse of the forward mapping updateRotationFromAngles() produces.
         */
        void setAnglesFromDirection(const glm::vec3& dir);

        /**
         * @brief Place the camera @p distance back from @p target along
         * @p dirToCamera (unit, pointing from target toward the camera) and aim
         * it at @p target. Shared core of focusOn() / viewFrom().
         */
        void placeCamera(Transform& transform, const glm::vec3& target,
                         const glm::vec3& dirToCamera, float distance);

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

