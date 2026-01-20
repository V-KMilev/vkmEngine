#include "input_handle.h"

#include "glfw_include.h"

namespace Engine {

void KeyboardInputHandle::update(GLFWwindow* window) {
    m_prevKeyState = m_keyState;

    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        int state = glfwGetKey(window, key);
        m_keyState[key] = (state == GLFW_PRESS || state == GLFW_REPEAT);
    }
}

bool KeyboardInputHandle::isKeyPressed(int key) const {
    auto it = m_keyState.find(key);
    return it != m_keyState.end() && it->second;
}

bool KeyboardInputHandle::isKeyReleased(int key) const {
    auto it = m_prevKeyState.find(key);
    bool prev = (it != m_prevKeyState.end()) ? it->second : false;
    return prev && !isKeyPressed(key);
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
    return m_buttonState[button];
}

bool MouseInputHandle::isButtonReleased(int button) const {
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

void InputHandle::update(GLFWwindow* window) {
    m_keyboardHandle.update(window);
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