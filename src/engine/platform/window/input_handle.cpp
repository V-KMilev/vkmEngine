#include "platform/window/input_handle.h"

#include <cstring>

#include "platform/window/glfw_include.h"

namespace Engine {

void KeyboardInputHandle::update() {
    std::memcpy(m_prevKeyState, m_keyState, sizeof(m_keyState));
}

void KeyboardInputHandle::onKeyEvent(int key, bool pressed) {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        m_keyState[key] = pressed;
    }
}

bool KeyboardInputHandle::isKeyPressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keyState[key];
}

bool KeyboardInputHandle::isKeyReleased(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_prevKeyState[key] && !m_keyState[key];
}

void MouseInputHandle::update(GLFWwindow* window) {
    m_prevButtonState[GLFW_MOUSE_BUTTON_LEFT] = m_buttonState[GLFW_MOUSE_BUTTON_LEFT];
    m_prevButtonState[GLFW_MOUSE_BUTTON_RIGHT] = m_buttonState[GLFW_MOUSE_BUTTON_RIGHT];

    for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
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
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return m_buttonState[button];
}

bool MouseInputHandle::isButtonReleased(int button) const {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return m_prevButtonState[button] && !m_buttonState[button];
}

void MouseInputHandle::setScrollDelta(double xOffset, double yOffset) {
    m_scrollX += xOffset;
    m_scrollY += yOffset;
}

void MouseInputHandle::resetScrollDelta() {
    m_scrollX = 0.0;
    m_scrollY = 0.0;
}

void MouseInputHandle::setupScrollCallback(GLFWwindow* window, InputHandle* inputHandle) {
    if (!window || !inputHandle) return;

    glfwSetWindowUserPointer(window, this);

    glfwSetScrollCallback(window, [](GLFWwindow* w, double xOffset, double yOffset) {
        auto* mouse = static_cast<MouseInputHandle*>(glfwGetWindowUserPointer(w));
        if (mouse) {
            mouse->setScrollDelta(xOffset, yOffset);
        }
    });
}

void InputHandle::setupCallbacks(GLFWwindow* window) {
    if (!window) return;

    // Store InputHandle* as user pointer for all GLFW callbacks
    glfwSetWindowUserPointer(window, this);

    // Key callback - updates keyboard state directly, no polling needed
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
        auto* input = static_cast<InputHandle*>(glfwGetWindowUserPointer(w));
        if (input) {
            bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
            input->m_keyboardHandle.onKeyEvent(key, pressed);
        }
    });

    // Scroll callback
    glfwSetScrollCallback(window, [](GLFWwindow* w, double xOffset, double yOffset) {
        auto* input = static_cast<InputHandle*>(glfwGetWindowUserPointer(w));
        if (input) {
            input->m_mouseHandle.setScrollDelta(xOffset, yOffset);
        }
    });
}

void InputHandle::update(GLFWwindow* window) {
    m_mouseHandle.update(window);
}

bool InputHandle::isPressed(int key) const {
    if (key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST) {
        return m_keyboardHandle.isKeyPressed(key);
    }
    return m_mouseHandle.isButtonPressed(key);
}
bool InputHandle::isReleased(int key) const {
    if (key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST) {
        return m_keyboardHandle.isKeyReleased(key);
    }
    return m_mouseHandle.isButtonReleased(key);
}

} // namespace Engine
