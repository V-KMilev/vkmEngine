#include "window.h"

#include <algorithm>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #undef ERROR
    #undef WARNING
#endif

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "logger.h"
#include "print_helper.h"

GLFWmonitor* getCurrentMonitor(GLFWwindow* window) {
    int windowX, windowY, windowWidth, windowHeight;
    glfwGetWindowPos(window, &windowX, &windowY);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    int monitorCount;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

    GLFWmonitor* bestMonitor = nullptr;
    int bestOverlap = 0;

    for (int i = 0; i < monitorCount; i++) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        int monitorX, monitorY;
        glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

        int overlapX = max(0, min(windowX + windowWidth, monitorX + mode->width) - max(windowX, monitorX));
        int overlapY = max(0, min(windowY + windowHeight, monitorY + mode->height) - max(windowY, monitorY));
        int overlap = overlapX * overlapY;

        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            bestMonitor = monitors[i];
        }
    }
    
    return bestMonitor ? bestMonitor : glfwGetPrimaryMonitor();
}

Window::~Window() {
    cleanup();
    glfwTerminate();
}

Window::Window(
    const std::string& title,
    int width,
    int height
) : m_title(title),
    m_width(width),
    m_height(height),
    m_refreshRate(0),
    m_windowMode(WindowMode::WINDOWED),
    m_window(nullptr)
{
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OPENGL_MAJOR_VERSION);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OPENGL_MINOR_VERSION);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor* mode = NULL;

    m_window = glfwCreateWindow(
        m_width,
        m_height,
        m_title.c_str(),
        mode,
        nullptr
    );
}

int Window::getWidth() const { return m_width; }
int Window::getHeight() const { return m_height; }
int Window::getRefreshRate() const { return m_refreshRate; }

void Window::setWindowMode(WindowMode windowMode) { m_windowMode = windowMode; }

void Window::cleanup() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

bool Window::updateMode() {
    if (!m_window) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    GLFWmonitor* monitor = getCurrentMonitor(m_window);
    if (!monitor) {
        LOG_ERROR("Failed to get current monitor");
        return false;
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return false;
    }

    m_width = mode->width;
    m_height = mode->height;
    m_refreshRate = mode->refreshRate;

    switch(m_windowMode) {
        case WindowMode::FULLSCREEN:
            break;
        case WindowMode::WINDOWED:
            monitor = nullptr;
            break;
        default:
            LOG_ERROR("Invalid window mode: %d", enumToString(m_windowMode));
            return false;
    }

    // Set window to the coresponding mode
    glfwSetWindowMonitor(
        m_window,
        monitor,
        0,
        0,
        m_width,
        m_height,
        m_refreshRate
    );

    return true;
}

bool Window::updateWidthHeightOnResize() {
    if (!m_window) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    glfwGetFramebufferSize(m_window, &m_width, &m_height);
    return true;
}

bool Window::swapBuffers() {
    if (!m_window) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    glfwSwapBuffers(m_window);
    return true;
}