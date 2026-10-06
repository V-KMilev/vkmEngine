#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "core/host_chrome.h"
#include "core/system.h"
#include "platform/input/input_map.h"
#include "system/visibility/host_view.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Action names the fly camera reads.
 *
 * Constants so a typo is a compile error; the map is keyed by the string.
 */
namespace CameraActions {
    inline constexpr const char* MOVE_FORWARD = "Camera/Forward";  ///< Axis: +forward, -back.
    inline constexpr const char* MOVE_RIGHT   = "Camera/Right";    ///< Axis: +right, -left.
    inline constexpr const char* MOVE_UP      = "Camera/Up";       ///< Axis: +up, -down.
    inline constexpr const char* BOOST        = "Camera/Boost";    ///< Held: move faster.
    inline constexpr const char* LOOK         = "Camera/Look";     ///< Held: steer and zoom.
} // namespace CameraActions

/**
 * @brief Where the editor looks at a scene from: a pose of its own, not an entity.
 *
 * Kept per scene in `editor_settings.json`. The fly camera has no roll, so yaw
 * and pitch are its whole orientation; its projection is perspective or orthographic.
 */
struct EditorViewpoint {
    /**
     * @brief Where the view stands; the default is where a scene with no camera
     *        is first seen from.
     *
     * Back along +Z, so the default level gaze (forward is -Z) faces the origin.
     */
    glm::vec3 position = glm::vec3(0.0f, 2.0f, 6.0f);
    float     yaw      = 0.0f;   ///< Radians about world up; see Math::fromYawPitch.
    float     pitch    = 0.0f;   ///< Radians above the horizon.

    bool  orthographic = false;  ///< Parallel projection, zoomed by orthoHeight.
    float orthoHeight  = 10.0f;  ///< Half the view's height in world units, when orthographic.
};

/**
 * @brief The editor's own view of the scene, and the fly controls that move it.
 *
 * The viewpoint is not an entity: looking around never dirties the scene, and a
 * running game's camera is never fought over. It reaches the frame as
 * FrameContext::hostView, rendered in place of the scene's active camera.
 *
 * Runs in the Input stage, or the viewport would lag the pointer a frame. The fly
 * actions live in the controller's own InputMap, sampled against what the host's
 * panels hold (setCapture), so an ejected session flies while the game hears nothing.
 */
class CameraControllerSystem : public System {
    public:
        /**
         * @brief Tunable feel parameters for movement, look, and zoom.
         */
        struct Settings {
            /// Units dollied per wheel notch while looking; orthographic zooms by the same dolly.
            float zoomSensitivity  = 0.04f;
            float lookSensitivity  = 0.002f;    ///< Multiplier for yaw/pitch rotation.
            float moveSpeed        = 10.0f;     ///< Units per second.
            float speedBoost       = 3.0f;      ///< Move multiplier while Boost is held.

            float minPitch = -PITCH_LIMIT;      ///< Degrees, at or below the horizon.
            float maxPitch = PITCH_LIMIT;       ///< Degrees, at or above the horizon.

            /// Furthest either pitch limit may reach from the horizon, in degrees.
            static constexpr float PITCH_LIMIT = 90.0f;

            /**
             * @brief @p settings with each pitch limit on its own side of the horizon.
             *
             * The pitch clamp is undefined when low exceeds high, and the file is
             * hand-editable.
             *
             * @param settings As read.
             * @return minPitch in [-PITCH_LIMIT, 0], maxPitch in [0, PITCH_LIMIT].
             */
            static Settings bounded(Settings settings);
        };

        CameraControllerSystem();
        ~CameraControllerSystem() override = default;

        CameraControllerSystem(const CameraControllerSystem& other) = delete;
        CameraControllerSystem& operator=(const CameraControllerSystem& other) = delete;

        CameraControllerSystem(CameraControllerSystem && other) = delete;
        CameraControllerSystem& operator=(CameraControllerSystem && other) = delete;

    public:
        /**
         * @brief Fly the viewpoint from this frame's input and publish the view.
         *
         * Flies only while no scene camera is looked through. Stood down, it
         * publishes nothing, so the frame renders through the game's camera.
         *
         * @param ctx Supplies scene, window and clock; receives hostView.
         */
        void update(FrameContext& ctx) override;

        Settings&       getSettings()       { return m_settings; }
        const Settings& getSettings() const { return m_settings; }
        void setSettings(const Settings& s) { m_settings = s; }
        bool isLooking() const { return m_looking; }

        /**
         * @brief Whether the viewport is the editor's this frame, or the game's.
         *
         * Stood down, it publishes no view, reads no input, leaves the cursor
         * alone, and refuses focusOn and viewFrom.
         *
         * @param active false to stand it down.
         */
        void setActive(bool active) { m_active = active; }

        /// See setActive.
        bool isActive() const { return m_active; }

        /**
         * @brief Declare what the host's own panels hold, for the fly camera's input.
         *
         * See InputOwnership::panelsHoldPointer against hostHoldsPointer.
         *
         * @param pointer  Host UI is under the cursor or dragging.
         * @param keyboard Host UI is taking text input.
         */
        void setCapture(bool pointer, bool keyboard) { m_capture.setCapture(pointer, keyboard); }

        /**
         * @brief Render through a scene camera instead of the viewpoint, or go back to it.
         *
         * Read-only; the camera need not be active. Dropped when the entity stops
         * being a camera with a pose, and by focusOn and viewFrom.
         *
         * @param camera Camera to look through, or {} for the viewpoint.
         */
        void lookThrough(EntityId camera) { m_view.through = camera; }

        /// {} while the viewpoint is shown.
        EntityId lookingThrough() const { return m_view.through; }

        const EditorViewpoint& viewpoint() const { return m_viewpoint; }

        /**
         * @brief Switch the projection, keeping what is in focus the same size.
         *
         * The focus is the point focusOn or viewFrom last aimed at, carried along as the view
         * flies: perspective sees it at its distance through the field of view, orthographic
         * frames the same height around it.
         *
         * @param orthographic True for a parallel projection.
         */
        void setOrthographic(bool orthographic);

        /// See setOrthographic.
        bool isOrthographic() const { return m_viewpoint.orthographic; }

        /**
         * @brief Put the viewpoint somewhere, as a scene's saved one is restored.
         *
         * @param viewpoint Pose to take.
         */
        void setViewpoint(const EditorViewpoint& viewpoint) { m_viewpoint = viewpoint; }

        /**
         * @brief Frame a scene this editor has not looked at before.
         *
         * From its active camera's pose; with none, from a default facing the origin.
         *
         * @param scene Being opened.
         */
        void startFrom(const Scene& scene);

        /**
         * @brief Move the viewpoint to focus on a target position from a given distance.
         *
         * @param target   World-space point to center on.
         * @param distance Pull-back along the view direction.
         */
        void focusOn(const glm::vec3& target, float distance);

        /**
         * @brief Snap the viewpoint to look at @p target from a world-space direction.
         *
         * @param target    World-space point to look at.
         * @param direction From @p target toward the viewpoint; (1,0,0) views from +X.
         * @param distance  How far back along @p direction to sit.
         */
        void viewFrom(const glm::vec3& target, const glm::vec3& direction, float distance);

        /**
         * @brief viewFrom along an axis, orthographic until the view is turned.
         *
         * A view down an axis is for lining things up, which perspective skews. Turning it
         * returns to perspective, unless the projection was switched by hand.
         *
         * @param target    World-space point to look at.
         * @param direction From @p target toward the viewpoint, along a world axis.
         * @param distance  The distance whose perspective view the orthographic zoom matches.
         * @param clearance How far back the viewpoint must sit to clear the scene, which an
         *                  orthographic near plane would otherwise cut.
         */
        void viewAlongAxis(
            const glm::vec3& target,
            const glm::vec3& direction,
            float distance,
            float clearance
        );

    private:
        /**
         * @brief Apply this frame's look, dolly and move to the viewpoint.
         *
         * @param ctx Supplies window and clock.
         */
        void fly(FrameContext& ctx);

        /**
         * @brief Place the viewpoint back from @p target and aim it at @p target.
         *
         * @param target      World-space point to look at.
         * @param dirToCamera Unit direction from target toward the viewpoint.
         * @param distance    How far back along @p dirToCamera to sit.
         */
        void place(const glm::vec3& target, const glm::vec3& dirToCamera, float distance);

        /**
         * @brief Move the focus @p dolly units nearer: the viewpoint, or the orthographic zoom.
         *
         * @param dolly   World units toward the focus; negative backs away.
         * @param forward The view direction.
         */
        void dolly(float dolly, const glm::vec3& forward);

        /// tan(fovY / 2): the view's half-height one unit in front of it.
        float halfHeightPerUnit() const;

    private:
        InputMap        m_input;      ///< The fly actions, apart from the game's map.
        HostChrome      m_capture;    ///< Only its capture is read.
        HostView        m_view;       ///< What a frame publishes; its free half mirrors m_viewpoint.
        EditorViewpoint m_viewpoint;

        Settings m_settings;

        float m_focusDistance = 10.0f;  ///< From the viewpoint to its focus; see setOrthographic.

        bool m_looking   = false;  ///< Look gesture held; drives the cursor mode.
        bool m_active    = true;   ///< False while stood down; see setActive.
        bool m_axisOrtho = false;  ///< Orthographic because of viewAlongAxis; turning ends it.
};

} // namespace Vkm::Engine
