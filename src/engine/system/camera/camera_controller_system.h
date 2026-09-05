#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "core/system.h"

namespace Vkm::Engine {
    class InputMap;

struct Transform;

/**
 * @brief Camera controller used in the editor, supporting free-fly and look controls.
 *
 * Runs in the Input stage, before every system that reads a camera: it turns
 * this frame's device state into a Transform, and a viewport that resolved
 * world matrices in the Transform stage from last frame's camera would lag the
 * pointer by a frame on every drag.
 *
 * An authoring tool, so an authoring host is what registers it: right-drag
 * hides and grabs the pointer, and a shipped game owns its own camera and
 * cursor. A runtime never creates one, which is a stronger statement than
 * creating one switched off.
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
        void setCameraEntity(EntityId cameraEntity) { m_cameraEntity = cameraEntity; }

        /**
         * @brief The entity the controller is currently flying.
         *
         * Always the active rendered camera. The editor uses it to suppress the
         * transform gizmo on that entity, a gizmo there fighting the fly
         * controls for the same drag.
         */
        EntityId getCameraEntity() const { return m_cameraEntity; }

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
         * @brief Snap the camera to look at @p target from a world-space direction.
         *
         * Used by the navigation-gizmo view presets, which pass an axis:
         * (1,0,0) is "view from +X".
         */
        void viewFrom(Scene& scene, const glm::vec3& target, const glm::vec3& direction, float distance);

        /**
         * @brief Whether this controller has moved the camera since last asked,
         * clearing the answer as it gives it.
         *
         * The camera it flies is the scene's own Camera entity - the editor has
         * none of its own, which is what makes "you move what you see" true -
         * and that entity's Transform is a value the scene file stores. So a
         * look around is an edit to authored data, and a host that tracks
         * unsaved work has to hear about it or the next save quietly writes
         * wherever the viewport was parked over the framing somebody chose.
         *
         * Asked rather than announced, because this controller has no editor to
         * tell: the runtime flies the same one and never asks. Covers every
         * write it makes - a fly drag, a scroll dolly, Frame Selected, a
         * view-cube snap - and reports nothing for a right-drag that moved the
         * pointer nowhere.
         *
         * @return true if the camera's Transform changed under this controller.
         */
        bool takeCameraMoved();

    private:
        /**
         * @brief Apply this frame's look, dolly and move to a camera pose.
         *
         * @param ctx      The frame's context: window, input, chrome and clock.
         * @param position Camera position, moved in place.
         * @param rotation Camera rotation, overwritten while looking.
         */
        void updateFlyMode(FrameContext& ctx, glm::vec3& position, glm::quat& rotation);

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
         * @brief Resolve the camera the editor renders through.
         *
         * The entity whose Camera component is `active`, and which carries a
         * Transform. Falls back to the current entity, so the view never dies
         * mid-edit.
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
         * @brief Place the camera back from @p target and aim it at @p target.
         *
         * The shared core of focusOn() and viewFrom().
         *
         * @param transform Camera transform to write.
         * @param target Point to aim at.
         * @param dirToCamera Unit direction pointing from target toward camera.
         * @param distance How far back along @p dirToCamera to sit.
         */
        void placeCamera(Transform& transform, const glm::vec3& target,
                         const glm::vec3& dirToCamera, float distance);

    private:
        EntityId m_cameraEntity{};
        EntityId m_lastDrivenId{};   ///< Detects a camera switch -> reseed angles

        Settings m_settings;

        float m_yaw   = 0.0f;
        float m_pitch = 0.0f;
        bool m_isRightMousePressed = false;

        /**
         * @brief Set whenever a write of this controller's changed the camera's
         * pose; cleared by takeCameraMoved().
         */
        bool m_cameraMoved         = false;
};

} // namespace Vkm::Engine
