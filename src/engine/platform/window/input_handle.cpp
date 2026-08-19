#include "platform/window/input_handle.h"

#include "platform/window/glfw_include.h"

#include "platform/window/window_manager.h"

namespace Vkm::Engine {

// The header sizes m_keyState[] / m_buttonState[] without including GLFW, so a
// GLFW that grows a key would leave every array short. This is the one place
// the real header is in scope to say so.
static_assert(MAX_KEY          == GLFW_KEY_LAST,
              "MAX_KEY is out of step with this GLFW - resize the key arrays");
static_assert(MAX_MOUSE_BUTTON == GLFW_MOUSE_BUTTON_LAST,
              "MAX_MOUSE_BUTTON is out of step with this GLFW - resize the button arrays");

namespace {
// Fetch the WindowManager stored as the GLFW user pointer; it owns the
// InputHandle the (necessarily capture-free) callbacks below have to reach.
WindowManager* windowManager(GLFWwindow* w) {
    return static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
}
} // namespace

void KeyboardInputHandle::onKeyEvent(int key, bool pressed) {
    if (key >= 0 && key <= MAX_KEY) {
        m_keyState[key] = pressed;
    }
}

bool KeyboardInputHandle::isKeyPressed(int key) const {
    if (key < 0 || key > MAX_KEY) return false;
    return m_keyState[key];
}

void MouseInputHandle::update(GLFWwindow* window) {
    for (int button = GLFW_MOUSE_BUTTON_1; button <= MAX_MOUSE_BUTTON; ++button) {
        int state = glfwGetMouseButton(window, button);
        m_buttonState[button] = (state == GLFW_PRESS);
    }

    double newX, newY;
    glfwGetCursorPos(window, &newX, &newY);

    m_deltaX = newX - m_x;
    m_deltaY = newY - m_y;

    m_x = newX;
    m_y = newY;
}

bool MouseInputHandle::isButtonPressed(int button) const {
    if (button < 0 || button > MAX_MOUSE_BUTTON) return false;
    return m_buttonState[button];
}

void MouseInputHandle::setScrollDelta(double yOffset) {
    m_scrollY += yOffset;
}

void MouseInputHandle::resetScrollDelta() {
    m_scrollY = 0.0;
}

void InputHandle::setupCallbacks(GLFWwindow* window) {
    if (!window) return;

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
        if (auto* manager = windowManager(w)) {
            const bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
            manager->getInputHandle().m_keyboardHandle.onKeyEvent(key, pressed);
        }
    });

    // Horizontal scroll is unused.
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double yOffset) {
        if (auto* manager = windowManager(w)) {
            manager->getInputHandle().m_mouseHandle.setScrollDelta(yOffset);
        }
    });
}

void InputHandle::update(GLFWwindow* window) {
    m_mouseHandle.update(window);
}

} // namespace Vkm::Engine
