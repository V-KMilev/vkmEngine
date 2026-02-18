#include "platform/window/window_manager.h"

#include "platform/window/glfw_include.h"

#include "platform/window/window.h"
#include "platform/window/input_handle.h"
#include "platform/window/frame_limiter.h"

#include "logger.h"
#include "debug/print_helper.h"

namespace Engine {

WindowManager::WindowManager() = default;

namespace {
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

            // int overlapX = max(0, min(windowX + windowWidth, monitorX + mode->width) - max(windowX, monitorX));
            // int overlapY = max(0, min(windowY + windowHeight, monitorY + mode->height) - max(windowY, monitorY));
            // int overlap = overlapX * overlapY;

            // if (overlap > bestOverlap) {
            //     bestOverlap = overlap;
            //     bestMonitor = monitors[i];
            // }
        }
        
        return bestMonitor ? bestMonitor : glfwGetPrimaryMonitor();
    }
}  // anonymous namespace

WindowManager::~WindowManager() {
    if (m_window) {
        m_window.reset();
    }
}

void WindowManager::createWindow(const std::string& title) {
    m_window = std::make_unique<Window>(title);
    m_inputHandle = std::make_unique<InputHandle>();
    m_frameLimiter = std::make_unique<FrameLimiter>();

    m_inputHandle->mouse().setupScrollCallback(m_window->getWindowContext(), m_inputHandle.get());
}

bool WindowManager::shouldClose() const {
    return glfwWindowShouldClose(m_window->getWindowContext());
}

bool WindowManager::requestClose() {
    glfwSetWindowShouldClose(m_window->getWindowContext(), GLFW_TRUE);
    return true;
}

bool WindowManager::swapBuffers() {
    GLFWwindow* windowContext = m_window->getWindowContext();

    if (!windowContext) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    glfwSwapBuffers(windowContext);

    // Apply frame limiting after swap
    m_frameLimiter->endFrame();

    return true;
}

bool WindowManager::updateMode(WindowMode windowMode) {
    GLFWwindow* windowContext = m_window->getWindowContext();

    if (!windowContext) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    GLFWmonitor* monitor = getCurrentMonitor(windowContext);
    if (!monitor) {
        LOG_ERROR("Failed to get current monitor");
        return false;
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return false;
    }

    switch(windowMode) {
        case WindowMode::FULLSCREEN:
            break;
        case WindowMode::WINDOWED:
            monitor = nullptr;
            break;
        default:
            LOG_ERROR("Invalid window mode: %s", enumToString(windowMode));
            return false;
    }

    // Set window to the coresponding mode
    glfwSetWindowMonitor(
        windowContext,
        monitor,
        0,
        0,
        mode->width,
        mode->height,
        mode->refreshRate
    );

    return true;
}

bool WindowManager::updateInput() {
    if (!m_inputHandle) {
        LOG_ERROR("Input handle is not initialized");
        return false;
    }

    GLFWwindow* windowContext = m_window->getWindowContext();

    if (!windowContext) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    // Reset scroll delta before polling new events
    m_inputHandle->mouse().resetScrollDelta();

    glfwPollEvents();

    m_inputHandle->update(windowContext);

    return true;
}

bool WindowManager::beginFrame() {
    m_frameLimiter->beginFrame();

    return !shouldClose();
}

void WindowManager::setVSync(bool enabled) {
    // Disable software limiting when using VSync
    m_frameLimiter->setUnlimited();
    m_window->setSwapInterval(enabled ? 1 : 0);
}

void WindowManager::setFramerate(int framerate) {
    // Disable VSync when using software limiting
    m_window->setSwapInterval(0);
    m_frameLimiter->setTargetFramerate(framerate);
}

void WindowManager::setCursorMode(CursorMode mode) {
    GLFWwindow* windowContext = m_window->getWindowContext();
    if (!windowContext) {
        LOG_ERROR("Cannot set cursor mode: window is not initialized");
        return;
    }

    auto glfwmode = GLFW_CURSOR_NORMAL;
    switch (mode) {
        case CursorMode::NORMAL:
            glfwmode = GLFW_CURSOR_NORMAL;
            break;
        case CursorMode::HIDDEN:
            glfwmode = GLFW_CURSOR_HIDDEN;
            break;
        case CursorMode::DISABLED:
            glfwmode = GLFW_CURSOR_DISABLED;
            break;
        case CursorMode::CAPTURED:
            glfwmode = GLFW_CURSOR_CAPTURED;
            break;
        default:
            LOG_ERROR("Invalid cursor mode: %d", mode);
            return;
    }
    glfwSetInputMode(windowContext, GLFW_CURSOR, glfwmode);
}

size_t WindowManager::getWidth() const {
    if (!m_window) {
        LOG_ERROR("Window is not initialized");
        return 0;
    }

    return m_window->getWidth();
}
size_t WindowManager::getHeight() const {
    if (!m_window) {
        LOG_ERROR("Window is not initialized");
        return 0;
    }

    return m_window->getHeight();
}

} // namespace Engine