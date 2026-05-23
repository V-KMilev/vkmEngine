#include "system/camera/camera_controller.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

#include "platform/window/window_manager.h"
#include "platform/window/input_handle.h"
#include "platform/window/glfw_include.h"

#include "debug/profiler.h"
#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/component/transform.h"
#include "ecs/component/camera.h"

namespace Engine {

CameraController::CameraController() = default;

SystemAccess CameraController::declareAccess() const {
    return SystemAccess{
        /*reads*/  { typeId<Camera>() },
        /*writes*/ { typeId<Transform>() },
    };
}

EntityId CameraController::resolveActiveCamera(Scene& scene) {
    // Fast path: keep flying the current camera while it stays the active one.
    EntityId cur = m_cameraEntity.getID();
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

void CameraController::reseedAnglesFromRotation(const glm::quat& rotation) {
    // Inverse of updateRotationFromAngles():
    //   forward = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    const glm::vec3 f = Math::computeForward(rotation);
    m_pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    m_yaw   = std::atan2(f.x, f.z);
}

void CameraController::update(FrameContext& ctx) {
    PROFILE_SCOPE("CameraController");
    // Always drive the active rendered camera. This keeps the fly controls
    // working after "Set as Main Camera" / a scene load (the old code flew a
    // fixed entity while the renderer used a different one).
    EntityId target = resolveActiveCamera(ctx.scene);
    if (!target || !ctx.scene.isAlive(target) || !ctx.scene.has<Transform>(target)) return;

    m_cameraEntity = Entity{target};
    auto& transform = ctx.scene.get<Transform>(target);

    // On a camera switch re-derive yaw/pitch from its current orientation so
    // the first look drag continues smoothly instead of snapping.
    if (!(m_lastDrivenId == target)) {
        reseedAnglesFromRotation(transform.rotation);
        m_lastDrivenId = target;
    }

    updateFlyMode(ctx.window, transform.position, transform.rotation, ctx.deltaTime);
}

void CameraController::updateFlyMode(WindowManager& windowManager, glm::vec3& position, glm::quat& rotation, float deltaTime) {
    auto& inputHandle   = windowManager.getInputHandle();
    auto& mouse    = inputHandle.getMouse();
    auto& keyboard = inputHandle.getKeyboard();

    // Right mouse: Look around (skip if editor UI wants mouse)
    bool isRightMousePressed = !m_editorWantsMouse && mouse.isButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);

    // Only update cursor mode when state changes
    if (isRightMousePressed != m_isRightMousePressed) {
        windowManager.setCursorMode(isRightMousePressed ? CursorMode::DISABLED : CursorMode::NORMAL);
        m_isRightMousePressed = isRightMousePressed;
    }

    if (!isRightMousePressed) {
        return;
    }

    // Update yaw and pitch
    m_yaw   -= static_cast<float>(mouse.getDeltaX()) * m_settings.lookSensitivity;
    m_pitch -= static_cast<float>(mouse.getDeltaY()) * m_settings.lookSensitivity;
    m_pitch = std::clamp(m_pitch, glm::radians(m_settings.minPitch), glm::radians(m_settings.maxPitch));

    updateRotationFromAngles(rotation, m_yaw, m_pitch);

    glm::vec3 forward = Math::computeForward(rotation);
    glm::vec3 right   = Math::computeRight(rotation);

    // Scroll wheel modifies forward/back
    float scrollDelta = static_cast<float>(mouse.getScrollY());
    if (std::abs(scrollDelta) > 0.001f) {
        position += forward * scrollDelta * m_settings.zoomSensitivity * m_settings.scrollMultiplier;
    }

    // Skip keyboard movement if editor UI wants keyboard
    if (m_editorWantsKeyboard) return;

    // Movement speed with optional boost
    float speed = m_settings.moveSpeed * deltaTime;
    if (keyboard.isKeyPressed(GLFW_KEY_LEFT_SHIFT)) {
        speed *= m_settings.speedBoost;
    }

    // WASD + QE movement
    if (keyboard.isKeyPressed(GLFW_KEY_W)) position += forward * speed;
    if (keyboard.isKeyPressed(GLFW_KEY_S)) position -= forward * speed;
    if (keyboard.isKeyPressed(GLFW_KEY_A)) position += right * speed;
    if (keyboard.isKeyPressed(GLFW_KEY_D)) position -= right * speed;
    if (keyboard.isKeyPressed(GLFW_KEY_Q)) position += Math::WORLD_AXIS_Y_UP * speed;
    if (keyboard.isKeyPressed(GLFW_KEY_E)) position -= Math::WORLD_AXIS_Y_UP * speed;
}

void CameraController::focusOn(Scene& scene, const glm::vec3& target, float distance) {
    EntityId camId = m_cameraEntity.getID();
    if (!camId || !scene.has<Transform>(camId)) return;

    auto& transform = scene.get<Transform>(camId);

    // Direction from target to current camera position
    glm::vec3 dir = transform.position - target;
    float len = glm::length(dir);
    if (len < 0.001f) {
        dir = -Math::computeForward(transform.rotation);
    } else {
        dir /= len;
    }

    transform.position = target + dir * distance;

    // Recompute yaw/pitch from the new look direction. This must invert the
    // exact forward mapping updateRotationFromAngles() produces:
    //   forward = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    // so pitch = asin(forward.y) and yaw = atan2(forward.x, forward.z).
    glm::vec3 lookDir = glm::normalize(target - transform.position);
    m_pitch = std::asin(std::clamp(lookDir.y, -1.0f, 1.0f));
    m_yaw   = std::atan2(lookDir.x, lookDir.z);

    updateRotationFromAngles(transform.rotation, m_yaw, m_pitch);
}

void CameraController::viewFrom(Scene& scene, const glm::vec3& target,
                                const glm::vec3& direction, float distance) {
    EntityId camId = m_cameraEntity.getID();
    if (!camId || !scene.has<Transform>(camId)) return;

    auto& transform = scene.get<Transform>(camId);

    const float dlen = glm::length(direction);
    if (dlen < 1e-6f) return;
    const glm::vec3 dir = direction / dlen;

    transform.position = target + dir * distance;
    const glm::vec3 lookDir = -dir;
    m_pitch = std::asin(std::clamp(lookDir.y, -1.0f, 1.0f));
    m_yaw   = std::atan2(lookDir.x, lookDir.z);
    updateRotationFromAngles(transform.rotation, m_yaw, m_pitch);
}

void CameraController::updateRotationFromAngles(glm::quat& rotation, float yaw, float pitch) {
    // Yaw rotates around world up axis
    glm::quat yawQuat = glm::angleAxis(yaw, Math::WORLD_AXIS_Y_UP);
    // Pitch rotates around local right axis (negative because mouse Y is inverted)
    glm::quat pitchQuat = glm::angleAxis(pitch, -Math::WORLD_AXIS_X_RIGHT);
    // Apply yaw first, then pitch (order matters for correct behavior)
    rotation = yawQuat * pitchQuat;
}

} // namespace Engine

