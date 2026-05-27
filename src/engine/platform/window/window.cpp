#define VKM_LOG_CATEGORY "WINDOW"

#include "platform/window/window.h"

#include <algorithm>

#include "logger.h"
#include "debug/print_helper.h"

#include <GL/glew.h>  // glewInit only - the GL function loader. Backend-agnostic callers don't need this.
#include "platform/window/glfw_include.h"

namespace Engine {

Window::~Window() {
    cleanup();
    glfwTerminate();

    LOG_TRACE("Destructed Window '%s'", m_title.c_str());
}

Window::Window(
    const std::string& title,
    int swapInterval
) : m_title(title),
    m_swapInterval(swapInterval),
    m_window(nullptr)
{
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OPENGL_MAJOR_VERSION);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OPENGL_MINOR_VERSION);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create a windowed mode window by default (pass NULL for monitor)
    m_window = glfwCreateWindow(
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        m_title.c_str(),
        NULL,
        NULL
    );

    if (!m_window) {
        LOG_ERROR("Failed to create window");
        throw std::runtime_error("Failed to create window");
    }

    // Make the context current (required for glewInit and glfwSwapInterval)
    glfwMakeContextCurrent(m_window);

    // Initialize GLEW to load GL function pointers (must happen after context is current).
    // Version / device strings are logged by the OpenGL backend when it constructs.
    if (glewInit() != GLEW_OK) {
        LOG_ERROR("Failed to initialize GLEW");
        throw std::runtime_error("Failed to initialize GLEW");
    }

    // 0 = Uncapped framerate
    // 1 = VSync enabled
    glfwSwapInterval(m_swapInterval);

    // Cache initial size (updated via GLFW window size callback)
    glfwGetWindowSize(m_window, &m_width, &m_height);

    LOG_TRACE("Constructed Window '%s'", m_title.c_str());
}

int Window::getWidth() const {
    return m_width;
}
int Window::getHeight() const {
    return m_height;
}

void Window::setSize(int width, int height) {
    m_width = width;
    m_height = height;
}

int Window::getRefreshRate() const {
    GLFWmonitor* monitor = glfwGetWindowMonitor(m_window);

    // If windowed, fall back to the primary monitor
    if (!monitor) {
        monitor = glfwGetPrimaryMonitor();
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return 0;
    }

    return mode->refreshRate;
}

GLFWwindow* Window::getWindowContext() const {
    return m_window;
}

void Window::setSwapInterval(int interval) {
    if (!m_window) {
        LOG_ERROR("Cannot set swap interval: window is not initialized");
        return;
    }

    glfwMakeContextCurrent(m_window);
    // 0 = Uncapped framerate
    // 1 = VSync enabled
    glfwSwapInterval(interval);
    m_swapInterval = interval;
}

void Window::cleanup() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
}

} // namespace Engine