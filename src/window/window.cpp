#include "window.h"

#include <algorithm>

#include "logger.h"
#include "print_helper.h"

#include "glfw_include.h"

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

    // By default, create a window on the primary and in fullscreen mode
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();

    m_window = glfwCreateWindow(
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        m_title.c_str(),
        monitor,
        NULL
    );

    if (!m_window) {
        LOG_ERROR("Failed to create window");
        throw std::runtime_error("Failed to create window");
    }

    // Make the context current (required for glfwSwapInterval)
    glfwMakeContextCurrent(m_window);

    // Initialize GLAD - must be done after making context current
    if (!gladLoadGL(glfwGetProcAddress)) {
        LOG_ERROR("Failed to initialize GLAD");
        throw std::runtime_error("Failed to initialize GLAD");
    }

    LOG_TRACE("OpenGL %s initialized", glGetString(GL_VERSION));

    // 0 = Uncapped framerate
    // 1 = VSync enabled
    glfwSwapInterval(m_swapInterval);

    LOG_TRACE("Constructed Window '%s'", m_title.c_str());
}

int Window::getWidth() const {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    return width;
}
int Window::getHeight() const {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    return height;
}
int Window::getRefreshRate() const {
    GLFWmonitor* monitor = glfwGetWindowMonitor(m_window);

    // If windowed, fall back to the primary monitor
    if (!monitor) {
        LOG_ERROR("Failed to get window monitor");
        return 0;
    }

    monitor = glfwGetPrimaryMonitor();

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
