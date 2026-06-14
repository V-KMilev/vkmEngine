#define VKM_LOG_CATEGORY "WINDOW"

#include "platform/window/window_manager.h"

#include <algorithm>

#include "platform/window/glfw_include.h"

#include "platform/window/window.h"
#include "platform/window/input_handle.h"
#include "platform/window/frame_limiter.h"

#include "logger.h"

#include "debug/profiler.h"

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

        int overlapX = std::max(0, std::min(windowX + windowWidth, monitorX + mode->width) - std::max(windowX, monitorX));
        int overlapY = std::max(0, std::min(windowY + windowHeight, monitorY + mode->height) - std::max(windowY, monitorY));
        int overlap = overlapX * overlapY;

        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            bestMonitor = monitors[i];
        }
    }
        
    return bestMonitor ? bestMonitor : glfwGetPrimaryMonitor();
}
}  // anonymous namespace

WindowManager::~WindowManager() = default;

void WindowManager::createWindow(const std::string& title) {
    m_window = std::make_unique<Window>(title);
    m_inputHandle = std::make_unique<InputHandle>();
    m_frameLimiter = std::make_unique<FrameLimiter>();

    m_inputHandle->setupCallbacks(m_window->getWindowContext(), m_window.get());
    LOG_INFO("Created window '%s' (%dx%d, refresh %dHz)",
        title.c_str(), m_window->getWidth(), m_window->getHeight(),
        m_window->getRefreshRate());
}

bool WindowManager::shouldClose() const {
    return glfwWindowShouldClose(m_window->getWindowContext());
}

bool WindowManager::requestClose() {
    LOG_INFO("Close requested");
    glfwSetWindowShouldClose(m_window->getWindowContext(), GLFW_TRUE);
    return true;
}

void WindowManager::cancelClose() {
    if (m_window) {
        LOG_VERBOSE("Pending close cancelled");
        glfwSetWindowShouldClose(m_window->getWindowContext(), GLFW_FALSE);
    }
}

void WindowManager::setTitle(const std::string& title) {
    if (m_window) glfwSetWindowTitle(m_window->getWindowContext(), title.c_str());
}

bool WindowManager::swapBuffers() {
    GLFWwindow* windowContext = m_window->getWindowContext();

    if (!windowContext) {
        LOG_ERROR("Window is not initialized");
        return false;
    }

    {
        // The actual present. With vsync off and no FPS cap this returns fast,
        // but when the CPU outruns the GPU the driver blocks here (or in the
        // next frame's first GL call) until the queue drains - so a fat
        // SwapBuffers zone is the tell-tale of a GPU-bound frame.
        PROFILE_SCOPE("SwapBuffers");
        glfwSwapBuffers(windowContext);
    }

    {
        // Deliberate cap sleep (only when setFramerate > 0); a separate zone so
        // a throttle sleep is never mistaken for a GPU-bound swap stall.
        PROFILE_SCOPE("FrameLimiter");
        m_frameLimiter->endFrame();
    }

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
        case WindowMode::Fullscreen:
            break;
        case WindowMode::Windowed:
            monitor = nullptr;
            break;
        default:
            LOG_ERROR("Invalid window mode: %s", toString(windowMode));
            return false;
    }

    // Set window to the corresponding mode
    glfwSetWindowMonitor(
        windowContext,
        monitor,
        0,
        0,
        mode->width,
        mode->height,
        mode->refreshRate
    );

    LOG_INFO("Mode -> %s (%dx%d @ %dHz)",
        toString(windowMode), mode->width, mode->height, mode->refreshRate);
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
    m_inputHandle->getMouse().resetScrollDelta();

    // Process GLFW events - key/scroll callbacks fire here
    glfwPollEvents();

    // Update mouse state (cursor position, button states)
    m_inputHandle->update(windowContext);

    return true;
}

bool WindowManager::beginFrame() {
    m_frameLimiter->beginFrame();

    return !shouldClose();
}

void WindowManager::setVSync(bool enabled) {
    // VSync and the software FPS cap are independent knobs. Set only what
    // the caller asked for; leave the framelimiter alone.
    m_window->setSwapInterval(enabled ? 1 : 0);
    LOG_INFO("VSync %s", enabled ? "ON" : "OFF");
}

void WindowManager::setFramerate(int framerate) {
    // VSync and the software FPS cap are independent. Setting a software
    // cap does not touch the swap interval; if both are active the lower
    // effective rate wins, which is the usual expectation.
    m_frameLimiter->setTargetFramerate(framerate);
    if (framerate > 0) {
        LOG_INFO("FPS cap = %d", framerate);
    } else {
        LOG_INFO("FPS cap removed (unlimited)");
    }
}

void WindowManager::setCursorMode(CursorMode mode) {
    GLFWwindow* windowContext = m_window->getWindowContext();
    if (!windowContext) {
        LOG_ERROR("Cannot set cursor mode: window is not initialized");
        return;
    }

    auto glfwmode = GLFW_CURSOR_NORMAL;
    switch (mode) {
        case CursorMode::Normal:
            glfwmode = GLFW_CURSOR_NORMAL;
            break;
        case CursorMode::Hidden:
            glfwmode = GLFW_CURSOR_HIDDEN;
            break;
        case CursorMode::Disabled:
            glfwmode = GLFW_CURSOR_DISABLED;
            break;
        case CursorMode::Captured:
            glfwmode = GLFW_CURSOR_CAPTURED;
            break;
        default:
            LOG_ERROR("Invalid cursor mode: %d", static_cast<int>(mode));
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

GLFWwindow* WindowManager::getWindowContext() const {
    return m_window ? m_window->getWindowContext() : nullptr;
}

void WindowManager::setSceneViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    m_sceneVpX = x;
    m_sceneVpY = y;
    m_sceneVpW = w;
    m_sceneVpH = h;
}

} // namespace Engine