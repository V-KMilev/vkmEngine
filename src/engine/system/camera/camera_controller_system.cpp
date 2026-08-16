#define VKM_LOG_CATEGORY "CAMERA"

#include "system/camera/camera_controller_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "logger.h"

#include "platform/window/window_manager.h"
#include "platform/window/input_handle.h"
#include "platform/window/glfw_include.h"
#include "platform/input/default_bindings.h"
#include "platform/input/input_map.h"

#include "debug/profiler.h"
#include "core/clock.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/camera.h"

namespace Engine {

CameraControllerSystem::CameraControllerSystem() = default;

EntityId CameraControllerSystem::resolveActiveCamera(Scene& scene) {
    // Fast path: keep flying the current camera while it stays the active one.
    EntityId cur = m_cameraEntity;
    if (cur && scene.isAlive(cur)
        && scene.has<Camera>(cur) && scene.has<Transform>(cur)
        && scene.get<Camera>(cur).active) {
        return cur;
    }

    // The camera the renderer uses is the one with Camera.active (same rule
    // as VisibilitySystem) - fly that, so "you move what you see".
    EntityId found{};
    scene.forEach<Camera, Transform>([&](EntityId id, const Camera& c, const Transform&) {
        if (found) return;
        if (c.active) found = id;
    });
    if (found) return found;

    // Nothing active (nothing renders anyway): keep the current entity so the
    // controller stays usable instead of going dead.
    if (cur && scene.isAlive(cur) && scene.has<Transform>(cur)) return cur;
    return {};
}

void CameraControllerSystem::setAnglesFromDirection(const glm::vec3& dir) {
    // Inverse of updateRotationFromAngles():
    //   forward = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    m_pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    m_yaw   = std::atan2(dir.x, dir.z);
}

void CameraControllerSystem::reseedAnglesFromRotation(const glm::quat& rotation) {
    setAnglesFromDirection(Math::computeForward(rotation));
}

void CameraControllerSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("CameraControllerSystem");
    if (!m_enabled) return;

    // Always drive the active rendered camera. This keeps the fly controls
    // working after "Set as Main Camera" / a scene load (the old code flew a
    // fixed entity while the renderer used a different one).
    // resolveActiveCamera only ever returns {} or a live entity that has a
    // Transform, so a non-empty result needs no further validation here.
    EntityId target = resolveActiveCamera(ctx.scene);
    if (!target) return;

    m_cameraEntity = target;
    auto& transform = ctx.scene.get<Transform>(target);

    // On a camera switch re-derive yaw/pitch from its current orientation so
    // the first look drag continues smoothly instead of snapping.
    if (!(m_lastDrivenId == target)) {
        reseedAnglesFromRotation(transform.rotation);
        m_lastDrivenId = target;
    }

    updateFlyMode(ctx.window, ctx.input, transform.position, transform.rotation, ctx.clock.getDeltaTime());
}

void CameraControllerSystem::updateFlyMode(WindowManager& windowManager, const InputMap& input,
                                           glm::vec3& position, glm::quat& rotation, float deltaTime) {
    auto& inputHandle = windowManager.getInputHandle();
    auto& mouse       = inputHandle.getMouse();

    // Right mouse: Look around (skip if editor UI wants mouse)
    bool isRightMousePressed = !m_editorWantsMouse && mouse.isButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);

    // Only update cursor mode when state changes
    if (isRightMousePressed != m_isRightMousePressed) {
        windowManager.setCursorMode(isRightMousePressed ? CursorMode::Disabled : CursorMode::Normal);
        m_isRightMousePressed = isRightMousePressed;
    }

    if (!isRightMousePressed) {
        return;
    }

    m_yaw   -= static_cast<float>(mouse.getDeltaX()) * m_settings.lookSensitivity;
    m_pitch -= static_cast<float>(mouse.getDeltaY()) * m_settings.lookSensitivity;
    m_pitch = std::clamp(m_pitch, glm::radians(m_settings.minPitch), glm::radians(m_settings.maxPitch));

    updateRotationFromAngles(rotation, m_yaw, m_pitch);

    glm::vec3 forward = Math::computeForward(rotation);
    glm::vec3 right   = Math::computeRight(rotation);

    float scrollDelta = static_cast<float>(mouse.getScrollY());
    if (std::abs(scrollDelta) > 0.001f) {
        position += forward * scrollDelta * m_settings.zoomSensitivity * m_settings.scrollMultiplier;
    }

    // Skip keyboard movement if editor UI wants keyboard
    if (m_editorWantsKeyboard) return;

    // Movement speed with optional boost
    float speed = m_settings.moveSpeed * deltaTime;
    if (input.held(InputActions::BOOST)) {
        speed *= m_settings.speedBoost;
    }

    // Axes rather than six key tests: opposing directions already cancel in the
    // map, and the binding (WASD here) is the project's to change.
    position += forward                * (input.axis(InputActions::MOVE_FORWARD) * speed);
    position += right                  * (input.axis(InputActions::MOVE_RIGHT)   * speed);
    position += Math::WORLD_AXIS_Y  * (input.axis(InputActions::MOVE_UP)      * speed);
}

void CameraControllerSystem::placeCamera(Transform& transform, const glm::vec3& target,
                                         const glm::vec3& dirToCamera, float distance) {
    transform.position = target + dirToCamera * distance;
    // dirToCamera points target -> camera, so the look direction is its negation.
    setAnglesFromDirection(-dirToCamera);
    updateRotationFromAngles(transform.rotation, m_yaw, m_pitch);
}

void CameraControllerSystem::focusOn(Scene& scene, const glm::vec3& target, float distance) {
    EntityId camId = m_cameraEntity;
    if (!camId || !scene.has<Transform>(camId)) return;

    auto& transform = scene.get<Transform>(camId);

    // Keep the current view direction, pulling back to `distance` from target.
    // Degenerate (camera sitting on target) -> back off along current forward.
    glm::vec3 dir = transform.position - target;
    const float len = glm::length(dir);
    dir = (len < 0.001f) ? -Math::computeForward(transform.rotation) : dir / len;

    placeCamera(transform, target, dir, distance);
    LOG_VERBOSE("FocusOn target=(%.2f,%.2f,%.2f) distance=%.2f",
        target.x, target.y, target.z, distance);
}

void CameraControllerSystem::viewFrom(Scene& scene, const glm::vec3& target, const glm::vec3& direction, float distance) {
    EntityId camId = m_cameraEntity;
    if (!camId || !scene.has<Transform>(camId)) return;

    auto& transform = scene.get<Transform>(camId);

    const float dlen = glm::length(direction);
    if (dlen < 1e-6f) return;
    const glm::vec3 dir = direction / dlen;

    placeCamera(transform, target, dir, distance);
    LOG_VERBOSE("ViewFrom target=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) distance=%.2f",
        target.x, target.y, target.z, dir.x, dir.y, dir.z, distance);
}

void CameraControllerSystem::updateRotationFromAngles(glm::quat& rotation, float yaw, float pitch) {
    // Yaw rotates around world up axis
    glm::quat yawQuat = glm::angleAxis(yaw, Math::WORLD_AXIS_Y);
    // Pitch rotates around local right axis (negative because mouse Y is inverted)
    glm::quat pitchQuat = glm::angleAxis(pitch, -Math::WORLD_AXIS_X);
    // Apply yaw first, then pitch (order matters for correct behavior)
    rotation = yawQuat * pitchQuat;
}

} // namespace Engine

