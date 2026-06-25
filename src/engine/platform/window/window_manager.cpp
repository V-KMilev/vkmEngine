#define VKM_LOG_CATEGORY "WINDOW"

#include "platform/window/window_manager.h"

#include <algorithm>
#include <stdexcept>

#include <GL/glew.h>  // glewInit only - the GL function loader. Backend-agnostic callers don't need this.
#include "platform/window/glfw_include.h"

#include "platform/window/input_handle.h"
#include "platform/window/frame_limiter.h"

#include "logger.h"

#include "debug/profiler.h"

#include "stb_image.h"

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

WindowManager::~WindowManager() {
    // Teardown order preserved from the former Window dtor + WindowManager dtor:
    // input/frame-limiter first (they outranked the window in member-destruction
    // order), then glfwDestroyWindow, then a single glfwTerminate.
    m_frameLimiter.reset();
    m_inputHandle.reset();

    if (m_windowHandle) {
        glfwDestroyWindow(m_windowHandle);
        m_windowHandle = nullptr;
    }
    glfwTerminate();

    LOG_TRACE("Destructed Window '%s'", m_title.c_str());
}

void WindowManager::createWindow(const std::string& title) {
    m_title = title;

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OPENGL_MAJOR_VERSION);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OPENGL_MINOR_VERSION);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create a windowed mode window by default (pass NULL for monitor)
    m_windowHandle = glfwCreateWindow(
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        m_title.c_str(),
        NULL,
        NULL
    );

    if (!m_windowHandle) {
        LOG_ERROR("Failed to create window");
        throw std::runtime_error("Failed to create window");
    }

    // Make the context current (required for glewInit and glfwSwapInterval)
    glfwMakeContextCurrent(m_windowHandle);

    // Initialize GLEW to load GL function pointers (must happen after context is current).
    // Version / device strings are logged by the OpenGL backend when it constructs.
    if (glewInit() != GLEW_OK) {
        LOG_ERROR("Failed to initialize GLEW");
        throw std::runtime_error("Failed to initialize GLEW");
    }

    // VSync off at creation (0 = uncapped). Set later via setVSync.
    glfwSwapInterval(0);

    // Cache initial size (updated via GLFW window size callback)
    glfwGetWindowSize(m_windowHandle, &m_width, &m_height);

    LOG_TRACE("Constructed Window '%s'", m_title.c_str());

    m_inputHandle = std::make_unique<InputHandle>();
    m_frameLimiter = std::make_unique<FrameLimiter>();

    m_inputHandle->setupCallbacks(m_windowHandle, this);
    LOG_INFO("Created window '%s' (%dx%d, refresh %dHz)",
        title.c_str(), m_width, m_height, getRefreshRate());
}

void WindowManager::setIcon(const std::string& path) {
    if (!m_windowHandle) {
        LOG_ERROR("setIcon called before createWindow - ignored");
        return;
    }
    int width, height, channels;
    // Force 4 channels (RGBA) - GLFWimage expects 32-bit RGBA, top-left origin.
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        LOG_ERROR("Window icon failed to load: '%s'", path.c_str());
        return;
    }
    GLFWimage image{ width, height, pixels };
    glfwSetWindowIcon(m_windowHandle, 1, &image);
    stbi_image_free(pixels);
    LOG_INFO("Window icon set from '%s' (%dx%d)", path.c_str(), width, height);
}

bool WindowManager::shouldClose() const {
    return glfwWindowShouldClose(m_windowHandle);
}

void WindowManager::requestClose() {
    LOG_INFO("Close requested");
    glfwSetWindowShouldClose(m_windowHandle, GLFW_TRUE);
}

void WindowManager::cancelClose() {
    if (m_windowHandle) {
        LOG_VERBOSE("Pending close cancelled");
        glfwSetWindowShouldClose(m_windowHandle, GLFW_FALSE);
    }
}

void WindowManager::setTitle(const std::string& title) {
    if (m_windowHandle) glfwSetWindowTitle(m_windowHandle, title.c_str());
}

void WindowManager::swapBuffers() {
    GLFWwindow* windowContext = m_windowHandle;

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
}

void WindowManager::updateMode(WindowMode windowMode) {
    GLFWwindow* windowContext = m_windowHandle;

    GLFWmonitor* monitor = getCurrentMonitor(windowContext);
    if (!monitor) {
        LOG_ERROR("Failed to get current monitor");
        return;
    }

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return;
    }

    // Target rect/refresh per mode. Fullscreen covers the whole monitor video
    // mode; Windowed is a centred rect at 75% of the monitor (a full-monitor
    // windowed rect at (0,0) would look like borderless fullscreen).
    int targetX = 0;
    int targetY = 0;
    int targetW = mode->width;
    int targetH = mode->height;
    int targetRefresh = mode->refreshRate;

    switch(windowMode) {
        case WindowMode::Fullscreen:
            break;
        case WindowMode::Windowed: {
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);
            monitor       = nullptr;
            targetW       = static_cast<int>(mode->width  * 0.75);
            targetH       = static_cast<int>(mode->height * 0.75);
            targetX       = monitorX + (mode->width  - targetW) / 2;
            targetY       = monitorY + (mode->height - targetH) / 2;
            targetRefresh = 0;  // ignored for windowed mode
            break;
        }
        default:
            LOG_ERROR("Invalid window mode: %s", toString(windowMode));
            return;
    }

    // Set window to the corresponding mode
    glfwSetWindowMonitor(
        windowContext,
        monitor,
        targetX,
        targetY,
        targetW,
        targetH,
        targetRefresh
    );

    LOG_INFO("Mode -> %s (%dx%d @ %dHz)",
        toString(windowMode), mode->width, mode->height, mode->refreshRate);
}

void WindowManager::updateInput() {
    GLFWwindow* windowContext = m_windowHandle;

    // Reset scroll delta before polling new events
    m_inputHandle->getMouse().resetScrollDelta();

    // Process GLFW events - key/scroll callbacks fire here
    glfwPollEvents();

    // Update mouse state (cursor position, button states)
    m_inputHandle->update(windowContext);
}

bool WindowManager::beginFrame() {
    m_frameLimiter->beginFrame();

    return !shouldClose();
}

void WindowManager::setVSync(bool enabled) {
    // VSync and the software FPS cap are independent knobs. Set only what
    // the caller asked for; leave the framelimiter alone.
    if (!m_windowHandle) {
        LOG_ERROR("Cannot set swap interval: window is not initialized");
        return;
    }

    glfwMakeContextCurrent(m_windowHandle);
    // 0 = Uncapped framerate
    // 1 = VSync enabled
    glfwSwapInterval(enabled ? 1 : 0);
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
    GLFWwindow* windowContext = m_windowHandle;
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
    if (!m_windowHandle) {
        LOG_ERROR("Window is not initialized");
        return 0;
    }

    return m_width;
}
size_t WindowManager::getHeight() const {
    if (!m_windowHandle) {
        LOG_ERROR("Window is not initialized");
        return 0;
    }

    return m_height;
}

void WindowManager::setSize(int width, int height) {
    m_width = width;
    m_height = height;
}

int WindowManager::getRefreshRate() const {
    GLFWmonitor* monitor = glfwGetWindowMonitor(m_windowHandle);

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

GLFWwindow* WindowManager::getWindowContext() const {
    return m_windowHandle;
}

void WindowManager::setSceneViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    m_sceneVpX = x;
    m_sceneVpY = y;
    m_sceneVpW = w;
    m_sceneVpH = h;
}

} // namespace Engine