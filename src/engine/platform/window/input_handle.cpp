// GLFW first so its real GLFW_KEY_LAST / GLFW_MOUSE_BUTTON_LAST are defined
// before input_handle.h's fallback copies, which would otherwise be redefined
// by glfw3.h (a warning). The static_assert below pins the two to each other.
#include "platform/window/glfw_include.h"

#include "platform/window/input_handle.h"
#include "platform/window/window_manager.h"

namespace Engine {

// input_handle.h sizes m_keyState[] / m_buttonState[] from fallback copies of
// these GLFW constants (it only forward-declares GLFWwindow, so the real header
// may not be in scope there). If a GLFW version ever changed them, the same
// class would get a different size across translation units - an ODR violation
// plus a buffer overrun. Pin the fallbacks to the real values here, where the
// real GLFW header is included.
static_assert(GLFW_KEY_LAST == 348, "GLFW_KEY_LAST changed; update the fallback in input_handle.h");
static_assert(GLFW_MOUSE_BUTTON_LAST == 7, "GLFW_MOUSE_BUTTON_LAST changed; update the fallback in input_handle.h");

namespace {
// Fetch the bundled callback pointers stored as the GLFW user pointer. Used by
// the (necessarily capture-free) GLFW callbacks below.
WindowCallbackData* callbackData(GLFWwindow* w) {
    return static_cast<WindowCallbackData*>(glfwGetWindowUserPointer(w));
}
} // namespace

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
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
        if (auto* data = callbackData(w); data && data->input) {
            const bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
            data->input->m_keyboardHandle.onKeyEvent(key, pressed);
        }
    });

    // Scroll callback (horizontal scroll unused)
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double yOffset) {
        if (auto* data = callbackData(w); data && data->input) {
            data->input->m_mouseHandle.setScrollDelta(yOffset);
        }
    });

    // Framebuffer-size callback - tracks the drawable size in pixels (what GL
    // viewports use), which also covers HiDPI / DPI changes a window-size callback
    // would miss. Instant updates on resize, no polling needed.
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        if (auto* data = callbackData(w); data && data->window) {
            data->window->setSize(width, height);
        }
    });
}

void InputHandle::update(GLFWwindow* window) {
    m_mouseHandle.update(window);
}

} // namespace Engine
