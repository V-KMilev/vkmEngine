#define VKM_LOG_CATEGORY "EDITOR"

#include "input/camera_controller_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "logger.h"

#include "platform/window/glfw_include.h"
#include "platform/window/window_manager.h"

#include "debug/profiler.h"
#include "core/clock.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/camera.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Install the fly camera's bindings into the controller's own map.
 *
 * @param map Populated.
 */
void installCameraBindings(InputMap& map) {
    // Paired keys on one axis, so opposing presses cancel in the map.
    const auto axis = [&](const char* action, int ahead, int back) {
        map.define(action, { InputBinding::key(ahead), InputBinding::key(back, -1.0f) });
    };
    axis(CameraActions::MOVE_FORWARD, GLFW_KEY_W, GLFW_KEY_S);
    axis(CameraActions::MOVE_RIGHT,   GLFW_KEY_D, GLFW_KEY_A);
    axis(CameraActions::MOVE_UP,      GLFW_KEY_Q, GLFW_KEY_E);
    map.define(CameraActions::BOOST, { InputBinding::key(GLFW_KEY_LEFT_SHIFT) });
    // Bound in the map, not read from the device, so it is sampled against the
    // same capture.
    map.define(CameraActions::LOOK,      { InputBinding::mouse(GLFW_MOUSE_BUTTON_RIGHT) });
    map.define(CameraActions::ORBIT,     { InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT) });
    map.define(CameraActions::PAN,       { InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE) });
    map.define(
        CameraActions::ORBIT_KEY,
        { InputBinding::key(GLFW_KEY_LEFT_ALT), InputBinding::key(GLFW_KEY_RIGHT_ALT) }
    );
}

/// The smallest orthographic half-height, and the nearest the focus comes, in world units.
constexpr float MIN_ORTHO_HEIGHT   = 0.01f;
constexpr float MIN_FOCUS_DISTANCE = 0.1f;

/// The least distance a notch dollies a share of, so a view at its focus still moves.
constexpr float MIN_DOLLY_REACH = 1.0f;

/// How long the fly speed shows after the wheel changes it, in seconds.
constexpr float SPEED_SHOWN_SECONDS = 1.5f;

} // namespace

CameraControllerSystem::Settings CameraControllerSystem::Settings::bounded(Settings settings) {
    settings.minPitch        = std::clamp(settings.minPitch, -PITCH_LIMIT, 0.0f);
    settings.maxPitch        = std::clamp(settings.maxPitch, 0.0f, PITCH_LIMIT);
    settings.dollyStep       = std::clamp(settings.dollyStep, MIN_DOLLY_STEP, MAX_DOLLY_STEP);
    settings.lookSensitivity = std::clamp(settings.lookSensitivity, MIN_LOOK, MAX_LOOK);
    settings.moveSpeed       = std::clamp(settings.moveSpeed, MIN_MOVE_SPEED, MAX_MOVE_SPEED);
    settings.speedBoost      = std::clamp(settings.speedBoost, MIN_SPEED_BOOST, MAX_SPEED_BOOST);
    return settings;
}

CameraControllerSystem::CameraControllerSystem() {
    installCameraBindings(m_input);
}

void CameraControllerSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("CameraControllerSystem");

    m_speedShown = std::max(m_speedShown - ctx.clock.getDeltaTime(), 0.0f);

    // A gesture held as the controller stood down gives the cursor back once.
    if (!m_active) {
        if (isNavigating()) ctx.window.setCursorMode(CursorMode::Normal);
        m_gesture = Gesture::None;
        return;
    }

    // A preview whose camera went away falls back to the viewpoint.
    const Scene& scene = ctx.scene;
    if (m_view.through && !(scene.has<Camera>(m_view.through) && scene.has<Transform>(m_view.through))) {
        m_view.through = {};
    }

    m_input.update(ctx.window.getInputHandle(), m_capture);
    if (!m_view.through) fly(ctx);
    else if (isNavigating()) {
        ctx.window.setCursorMode(CursorMode::Normal);
        m_gesture = Gesture::None;
    }

    m_view.position           = m_viewpoint.position;
    m_view.rotation           = Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch);
    m_view.camera.projection  = m_viewpoint.orthographic ? ProjectionType::Orthographic
                                                         : ProjectionType::Perspective;
    m_view.camera.orthoHeight = m_viewpoint.orthoHeight;
    ctx.hostView = &m_view;
}

CameraControllerSystem::Gesture CameraControllerSystem::nextGesture() const {
    switch (m_gesture) {
        case Gesture::Look:
            if (m_input.held(CameraActions::LOOK)) return Gesture::Look;
            break;
        case Gesture::Orbit:
            if (m_input.held(CameraActions::ORBIT)) return Gesture::Orbit;
            break;
        case Gesture::Pan:
            if (m_input.held(CameraActions::PAN)) return Gesture::Pan;
            break;
        case Gesture::None:
            break;
    }
    if (m_input.held(CameraActions::LOOK)) return Gesture::Look;
    // Pressed, not held: a left drag that began as a pick does not turn into an orbit.
    const bool orbit = m_input.pressed(CameraActions::ORBIT) && m_input.held(CameraActions::ORBIT_KEY);
    if (orbit) return Gesture::Orbit;
    if (m_input.pressed(CameraActions::PAN)) return Gesture::Pan;
    return Gesture::None;
}

void CameraControllerSystem::fly(FrameContext& ctx) {
    const Gesture gesture = nextGesture();
    if ((gesture != Gesture::None) != isNavigating()) {
        ctx.window.setCursorMode(gesture != Gesture::None ? CursorMode::Disabled : CursorMode::Normal);
    }
    m_gesture = gesture;

    const glm::vec2 delta = m_input.pointerDelta();
    const float     wheel = m_input.uiWheel();

    // A view snapped down an axis stays orthographic only until it is turned.
    const bool turning = gesture == Gesture::Look || gesture == Gesture::Orbit;
    if (m_axisOrtho && turning && (delta.x != 0.0f || delta.y != 0.0f)) setOrthographic(false);

    if (gesture == Gesture::Orbit) orbit(delta);
    if (gesture == Gesture::Pan)   pan(delta, static_cast<float>(ctx.chrome.viewport(ctx.window).height));
    if (gesture == Gesture::Look)  turn(delta);

    const glm::quat rotation = Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch);
    const glm::vec3 forward  = Math::computeForward(rotation);
    const glm::vec3 right    = Math::computeRight(rotation);

    // Looking, the wheel sets how fast the keys fly; otherwise it dollies toward the focus.
    if (std::abs(wheel) > glm::epsilon<float>()) {
        if (gesture == Gesture::Look) {
            const float range  = Settings::SPEED_RANGE;
            const float scaled = m_speedScale * std::pow(Settings::SPEED_STEP, wheel);
            m_speedScale = std::clamp(scaled, 1.0f / range, range);
            m_speedShown = SPEED_SHOWN_SECONDS;
        } else {
            // Orthographic, the distance perspective frames its height at, so a notch zooms by a share.
            const float reach = m_viewpoint.orthographic
                ? m_viewpoint.orthoHeight / halfHeightPerUnit()
                : std::max(m_focusDistance, MIN_DOLLY_REACH);
            dolly(wheel * m_settings.dollyStep * reach, forward);
        }
    }

    if (gesture != Gesture::Look) return;

    float speed = m_settings.moveSpeed * m_speedScale * ctx.clock.getDeltaTime();
    if (m_input.held(CameraActions::BOOST)) {
        speed *= m_settings.speedBoost;
    }

    glm::vec3& position = m_viewpoint.position;
    position += forward            * (m_input.axis(CameraActions::MOVE_FORWARD) * speed);
    position += right              * (m_input.axis(CameraActions::MOVE_RIGHT)   * speed);
    position += Math::WORLD_AXIS_Y * (m_input.axis(CameraActions::MOVE_UP)      * speed);
}

void CameraControllerSystem::turn(const glm::vec2& turn) {
    m_viewpoint.yaw   -= turn.x * m_settings.lookSensitivity;
    m_viewpoint.pitch -= turn.y * m_settings.lookSensitivity;
    m_viewpoint.pitch = std::clamp(
        m_viewpoint.pitch,
        glm::radians(m_settings.minPitch),
        glm::radians(m_settings.maxPitch)
    );
}

void CameraControllerSystem::orbit(const glm::vec2& turn) {
    const glm::vec3 before = Math::computeForward(Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch));
    const glm::vec3 focus  = m_viewpoint.position + before * m_focusDistance;
    this->turn(turn);
    const glm::vec3 after = Math::computeForward(Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch));
    m_viewpoint.position  = focus - after * m_focusDistance;
}

void CameraControllerSystem::pan(const glm::vec2& drag, float viewPixels) {
    // World units a pixel spans at the focus, so what is under the pointer stays under it.
    const float halfHeight = m_viewpoint.orthographic ? m_viewpoint.orthoHeight
                                                      : m_focusDistance * halfHeightPerUnit();
    const float perPixel   = 2.0f * halfHeight / std::max(viewPixels, 1.0f);

    const glm::quat rotation = Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch);
    m_viewpoint.position -= Math::computeRight(rotation) * (drag.x * perPixel);
    m_viewpoint.position += Math::computeUp(rotation)    * (drag.y * perPixel);
}

void CameraControllerSystem::startFrom(const Scene& scene) {
    const EntityId camera = findActiveCamera(scene);
    if (const Transform* transform = scene.tryGet<Transform>(camera)) {
        m_viewpoint.position = resolvedWorldPosition(scene, camera, *transform);
        Math::toYawPitch(
            Math::computeForward(resolvedWorldRotation(scene, camera, *transform)),
            m_viewpoint.yaw,
            m_viewpoint.pitch
        );
        return;
    }
    m_viewpoint.position = EditorViewpoint{}.position;
    Math::toYawPitch(-m_viewpoint.position, m_viewpoint.yaw, m_viewpoint.pitch);
}

void CameraControllerSystem::place(const glm::vec3& target, const glm::vec3& dirToCamera, float distance) {
    m_view.through       = {};
    m_viewpoint.position = target + dirToCamera * distance;
    m_focusDistance      = std::max(distance, MIN_FOCUS_DISTANCE);
    Math::toYawPitch(-dirToCamera, m_viewpoint.yaw, m_viewpoint.pitch);
}

void CameraControllerSystem::dolly(float dolly, const glm::vec3& forward) {
    if (m_viewpoint.orthographic) {
        // The height a perspective view would frame at the focus after the same dolly.
        m_viewpoint.orthoHeight = std::max(
            m_viewpoint.orthoHeight - dolly * halfHeightPerUnit(),
            MIN_ORTHO_HEIGHT
        );
        return;
    }
    m_viewpoint.position += forward * dolly;
    m_focusDistance = std::max(m_focusDistance - dolly, MIN_FOCUS_DISTANCE);
}

float CameraControllerSystem::halfHeightPerUnit() const {
    return std::tan(m_view.camera.fovY * 0.5f);
}

void CameraControllerSystem::setOrthographic(bool orthographic) {
    m_axisOrtho = false;
    if (orthographic == m_viewpoint.orthographic) return;

    if (orthographic) {
        m_viewpoint.orthoHeight = std::max(m_focusDistance * halfHeightPerUnit(), MIN_ORTHO_HEIGHT);
    } else {
        // Back to where perspective frames the focus at the height orthographic showed.
        const glm::quat rotation = Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch);
        const glm::vec3 forward  = Math::computeForward(rotation);
        const glm::vec3 focus    = m_viewpoint.position + forward * m_focusDistance;
        const float     distance = m_viewpoint.orthoHeight / halfHeightPerUnit();
        m_focusDistance          = std::max(distance, MIN_FOCUS_DISTANCE);
        m_viewpoint.position     = focus - forward * m_focusDistance;
    }
    m_viewpoint.orthographic = orthographic;
}

void CameraControllerSystem::focusOn(const glm::vec3& target, float distance) {
    if (!m_active) return;

    // Keep the view direction; with the viewpoint on target, back off along forward.
    glm::vec3 dir = m_viewpoint.position - target;
    const float len = glm::length(dir);
    dir = (len < glm::epsilon<float>())
        ? -Math::computeForward(Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch))
        : dir / len;

    place(target, dir, distance);
    if (m_viewpoint.orthographic) {
        m_viewpoint.orthoHeight = std::max(distance * halfHeightPerUnit(), MIN_ORTHO_HEIGHT);
    }
    LOG_VERBOSE("FocusOn target=(%.2f,%.2f,%.2f) distance=%.2f", target.x, target.y, target.z, distance);
}

void CameraControllerSystem::viewFrom(const glm::vec3& target, const glm::vec3& direction, float distance) {
    if (!m_active) return;

    const float dlen = glm::length(direction);
    if (dlen < glm::epsilon<float>()) return;
    const glm::vec3 dir = direction / dlen;

    place(target, dir, distance);
    LOG_VERBOSE(
        "ViewFrom target=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) distance=%.2f",
        target.x,
        target.y,
        target.z,
        dir.x,
        dir.y,
        dir.z,
        distance
    );
}

void CameraControllerSystem::viewAlongAxis(
    const glm::vec3& target,
    const glm::vec3& direction,
    float distance,
    float clearance
) {
    if (!m_active) return;

    // Perspective at `distance` sets the zoom; an orthographic view already has its own.
    const bool wasOrthographic = m_viewpoint.orthographic;
    viewFrom(target, direction, distance);
    if (!wasOrthographic) {
        setOrthographic(true);
        m_axisOrtho = true;
    }

    // Orthographic, standing further back changes nothing but what the near plane cuts;
    // within half the far plane, so what it looks at stays in range.
    const float standOff = std::min(std::max(distance, clearance), m_view.camera.zFar * 0.5f);
    m_viewpoint.position = target + glm::normalize(direction) * standOff;
    m_focusDistance      = standOff;
}

} // namespace Vkm::Engine
