#include "camera_controller.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

#include "core/engine.h"
#include "platform/window/input_handle.h"
#include "platform/window/glfw_include.h"

#include "ecs/component/transform.h"

namespace Engine {

CameraController::CameraController() = default;

void CameraController::update(FrameContext& ctx) {
    if (!ctx.scene.has<Transform>(m_cameraEntity.getID())) {
        return;
    }

    auto& transform = ctx.scene.get<Transform>(m_cameraEntity.getID());

    updateFlyMode(transform.position, transform.rotation, ctx.deltaTime);
}

void CameraController::updateFlyMode(glm::vec3& position, glm::quat& rotation, float deltaTime) {
    auto& windowManager = Engine::get().getWindow();
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

    // Calculate movement direction vectors using Transform helper methods
    glm::vec3 forward = Transform::computeForward(rotation);
    glm::vec3 right   = Transform::computeRight(rotation);

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
    if (keyboard.isKeyPressed(GLFW_KEY_Q)) position += WORLD_AXIS_Y_UP * speed;
    if (keyboard.isKeyPressed(GLFW_KEY_E)) position -= WORLD_AXIS_Y_UP * speed;
}

void CameraController::focusOn(Scene& scene, const glm::vec3& target, float distance) {
    EntityId camId = m_cameraEntity.getID();
    if (!camId || !scene.has<Transform>(camId)) return;

    auto& transform = scene.get<Transform>(camId);

    // Direction from target to current camera position
    glm::vec3 dir = transform.position - target;
    float len = glm::length(dir);
    if (len < 0.001f) {
        dir = -Transform::computeForward(transform.rotation);
    } else {
        dir /= len;
    }

    transform.position = target + dir * distance;

    // Recompute yaw/pitch from new look direction (consistent with updateRotationFromAngles)
    glm::vec3 lookDir = glm::normalize(target - transform.position);
    m_pitch = std::asin(std::clamp(-lookDir.y, -1.0f, 1.0f));
    m_yaw   = std::atan2(-lookDir.x, lookDir.z);

    updateRotationFromAngles(transform.rotation, m_yaw, m_pitch);
}

void CameraController::updateRotationFromAngles(glm::quat& rotation, float yaw, float pitch) {
    // Yaw rotates around world up axis
    glm::quat yawQuat = glm::angleAxis(yaw, WORLD_AXIS_Y_UP);
    // Pitch rotates around local right axis (negative because mouse Y is inverted)
    glm::quat pitchQuat = glm::angleAxis(pitch, -WORLD_AXIS_X_RIGHT);
    // Apply yaw first, then pitch (order matters for correct behavior)
    rotation = yawQuat * pitchQuat;
}

} // namespace Engine

