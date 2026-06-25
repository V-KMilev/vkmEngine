#include "platform/window/input_handle.h"

#include "platform/window/glfw_include.h"
#include "platform/window/window_manager.h"

namespace Engine {

void KeyboardInputHandle::onKeyEvent(int key, bool pressed) {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        m_keyState[key] = pressed;
    }
}

bool KeyboardInputHandle::isKeyPressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keyState[key];
}

void MouseInputHandle::update(GLFWwindow* window) {
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

void MouseInputHandle::setScrollDelta(double yOffset) {
    m_scrollY += yOffset;
}

void MouseInputHandle::resetScrollDelta() {
    m_scrollY = 0.0;
}

void InputHandle::setupCallbacks(GLFWwindow* window, WindowManager* windowManager) {
    if (!window) return;

    // Bundle both pointers so all GLFW callbacks can access input + window
    m_callbackData.input = this;
    m_callbackData.window = windowManager;
    glfwSetWindowUserPointer(window, &m_callbackData);

    // Key callback - updates keyboard state directly, no polling needed
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
        auto* data = static_cast<WindowCallbackData*>(glfwGetWindowUserPointer(w));
        if (data && data->input) {
            bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
            data->input->m_keyboardHandle.onKeyEvent(key, pressed);
        }
    });

    // Scroll callback
    glfwSetScrollCallback(window, [](GLFWwindow* w, double xOffset, double yOffset) {
        (void)xOffset;  // horizontal scroll unused
        auto* data = static_cast<WindowCallbackData*>(glfwGetWindowUserPointer(w));
        if (data && data->input) {
            data->input->m_mouseHandle.setScrollDelta(yOffset);
        }
    });

    // Window size callback - instant updates on resize, no polling needed
    glfwSetWindowSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        auto* data = static_cast<WindowCallbackData*>(glfwGetWindowUserPointer(w));
        if (data && data->window) {
            data->window->setSize(width, height);
        }
    });
}

void InputHandle::update(GLFWwindow* window) {
    m_mouseHandle.update(window);
}

} // namespace Engine
