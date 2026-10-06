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
 * @brief A keyboard binding contributing @p scale to its action's axis.
 *
 * @param code GLFW key.
 * @param scale Added to the axis per press.
 * @return The binding.
 */
InputBinding key(int code, float scale = 1.0f) {
    return InputBinding{InputSource::Key, code, scale};
}

/**
 * @brief Install the fly camera's bindings into the controller's own map.
 *
 * @param map Populated.
 */
void installCameraBindings(InputMap& map) {
    // Paired keys on one axis, so opposing presses cancel in the map.
    map.define(CameraActions::MOVE_FORWARD, { key(GLFW_KEY_W,  1.0f), key(GLFW_KEY_S, -1.0f) });
    map.define(CameraActions::MOVE_RIGHT,   { key(GLFW_KEY_D,  1.0f), key(GLFW_KEY_A, -1.0f) });
    map.define(CameraActions::MOVE_UP,      { key(GLFW_KEY_Q,  1.0f), key(GLFW_KEY_E, -1.0f) });
    map.define(CameraActions::BOOST,        { key(GLFW_KEY_LEFT_SHIFT) });
    // Bound in the map, not read from the device, so it is sampled against the
    // same capture.
    map.define(
        CameraActions::LOOK,
        { InputBinding{InputSource::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, 1.0f} }
    );
}

/// The smallest orthographic half-height, and the nearest the focus comes, in world units.
constexpr float MIN_ORTHO_HEIGHT   = 0.01f;
constexpr float MIN_FOCUS_DISTANCE = 0.1f;

} // namespace

CameraControllerSystem::Settings CameraControllerSystem::Settings::bounded(Settings settings) {
    settings.minPitch = std::clamp(settings.minPitch, -PITCH_LIMIT, 0.0f);
    settings.maxPitch = std::clamp(settings.maxPitch, 0.0f, PITCH_LIMIT);
    return settings;
}

CameraControllerSystem::CameraControllerSystem() {
    installCameraBindings(m_input);
}

void CameraControllerSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("CameraControllerSystem");

    // A look held as the controller stood down gives the cursor back once.
    if (!m_active) {
        if (m_looking) ctx.window.setCursorMode(CursorMode::Normal);
        m_looking = false;
        return;
    }

    // A preview whose camera went away falls back to the viewpoint.
    const Scene& scene = ctx.scene;
    if (m_view.through && !(scene.has<Camera>(m_view.through) && scene.has<Transform>(m_view.through))) {
        m_view.through = {};
    }

    m_input.update(ctx.window.getInputHandle(), m_capture);
    if (!m_view.through) fly(ctx);
    else if (m_looking) {
        ctx.window.setCursorMode(CursorMode::Normal);
        m_looking = false;
    }

    m_view.position           = m_viewpoint.position;
    m_view.rotation           = Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch);
    m_view.camera.projection  = m_viewpoint.orthographic ? ProjectionType::Orthographic
                                                         : ProjectionType::Perspective;
    m_view.camera.orthoHeight = m_viewpoint.orthoHeight;
    ctx.hostView = &m_view;
}

void CameraControllerSystem::fly(FrameContext& ctx) {
    const bool looking = m_input.held(CameraActions::LOOK);

    if (looking != m_looking) {
        ctx.window.setCursorMode(looking ? CursorMode::Disabled : CursorMode::Normal);
        m_looking = looking;
    }

    if (!looking) {
        return;
    }

    // A view snapped down an axis stays orthographic only until it is turned.
    const glm::vec2 turn = m_input.pointerDelta();
    if (m_axisOrtho && (turn.x != 0.0f || turn.y != 0.0f)) setOrthographic(false);

    m_viewpoint.yaw   -= m_input.pointerDelta().x * m_settings.lookSensitivity;
    m_viewpoint.pitch -= m_input.pointerDelta().y * m_settings.lookSensitivity;
    m_viewpoint.pitch = std::clamp(
        m_viewpoint.pitch,
        glm::radians(m_settings.minPitch),
        glm::radians(m_settings.maxPitch)
    );

    const glm::quat rotation = Math::fromYawPitch(m_viewpoint.yaw, m_viewpoint.pitch);
    const glm::vec3 forward  = Math::computeForward(rotation);
    const glm::vec3 right    = Math::computeRight(rotation);

    // Orthographic, the wheel zooms; the move keys still move, which is what clears a clip.
    const float scrollDelta = m_input.uiWheel();
    if (std::abs(scrollDelta) > glm::epsilon<float>()) {
        dolly(scrollDelta * m_settings.zoomSensitivity, forward);
    }

    float speed = m_settings.moveSpeed * ctx.clock.getDeltaTime();
    if (m_input.held(CameraActions::BOOST)) {
        speed *= m_settings.speedBoost;
    }

    glm::vec3& position = m_viewpoint.position;
    position += forward            * (m_input.axis(CameraActions::MOVE_FORWARD) * speed);
    position += right              * (m_input.axis(CameraActions::MOVE_RIGHT)   * speed);
    position += Math::WORLD_AXIS_Y * (m_input.axis(CameraActions::MOVE_UP)      * speed);
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
