#define VKM_LOG_CATEGORY "WINDOW"

#include "platform/window/window_manager.h"

#include <algorithm>
#include <stdexcept>

#include <GL/glew.h>
#include "platform/window/glfw_include.h"

#include "platform/window/input_handle.h"
#include "platform/window/frame_limiter.h"

#include "logger.h"

#include "debug/profiler.h"

#include "stb_image.h"

namespace Engine {

WindowManager::WindowManager() = default;

namespace {
// GLFW parks the reason for a failure in its per-thread error state, and nothing
// reads it unless asked - so a startup that dies here says what actually failed
// instead of naming the call that returned null.
const char* glfwErrorDescription() {
    const char* description = nullptr;
    glfwGetError(&description);
    return description ? description : "no description";
}

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
} // namespace

WindowManager::~WindowManager() {
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
        LOG_ERROR("Failed to initialize GLFW: %s", glfwErrorDescription());
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, OPENGL_MAJOR_VERSION);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, OPENGL_MINOR_VERSION);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Null monitor = windowed mode.
    m_windowHandle = glfwCreateWindow(
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        m_title.c_str(),
        nullptr,
        nullptr
    );

    if (!m_windowHandle) {
        // Usually a driver that cannot serve the core context hinted above, but
        // "no display" and "GLFW built without this platform" land here too -
        // only GLFW's own description tells them apart.
        LOG_ERROR("Failed to create window (requested OpenGL %d.%d core): %s",
            OPENGL_MAJOR_VERSION, OPENGL_MINOR_VERSION, glfwErrorDescription());
        throw std::runtime_error("Failed to create window");
    }

    // Required before glewInit and glfwSwapInterval below.
    glfwMakeContextCurrent(m_windowHandle);

    // Version / device strings are logged by the OpenGL backend when it constructs.
    if (const GLenum glewError = glewInit(); glewError != GLEW_OK) {
        LOG_ERROR("Failed to initialize GLEW: %s",
            reinterpret_cast<const char*>(glewGetErrorString(glewError)));
        throw std::runtime_error("Failed to initialize GLEW");
    }

    // VSync off at creation (0 = uncapped). Set later via setVSync.
    glfwSwapInterval(0);

    // Framebuffer pixels, not window screen coords: the two differ on a HiDPI /
    // scaled display, and GL viewports and render targets want the former.
    glfwGetFramebufferSize(m_windowHandle, &m_width, &m_height);

    LOG_TRACE("Constructed Window '%s'", m_title.c_str());

    // Every GLFW callback below reaches its target through this one pointer:
    // the framebuffer-size one lands here, the input ones carry on to
    // getInputHandle(). Registered before any of them, and well before the
    // first glfwPollEvents() that could dispatch one.
    glfwSetWindowUserPointer(m_windowHandle, this);

    // Framebuffer, not window, size: this also catches the HiDPI / DPI changes a
    // window-size callback would miss.
    glfwSetFramebufferSizeCallback(m_windowHandle, [](GLFWwindow* w, int width, int height) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->setSize(width, height);
        }
    });

    m_inputHandle.setupCallbacks(m_windowHandle);
    LOG_INFO("Created window '%s' (%dx%d, refresh %dHz)",
        title.c_str(), m_width, m_height, getRefreshRate());
}

void WindowManager::setIcon(const std::string& path) {
    if (!hasWindow("setIcon")) return;
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
    {
        // The actual present. With vsync off and no FPS cap this returns fast,
        // but when the CPU outruns the GPU the driver blocks here (or in the
        // next frame's first GL call) until the queue drains - so a fat
        // SwapBuffers zone is the tell-tale of a GPU-bound frame.
        PROFILE_SCOPE("SwapBuffers");
        glfwSwapBuffers(m_windowHandle);
    }

    {
        // Deliberate cap sleep (only when setFramerate > 0); a separate zone so
        // a throttle sleep is never mistaken for a GPU-bound swap stall.
        PROFILE_SCOPE("FrameLimiter");
        m_frameLimiter.endFrame();
    }
}

void WindowManager::updateMode(WindowMode windowMode) {
    GLFWmonitor* monitor = getCurrentMonitor(m_windowHandle);
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

    switch (windowMode) {
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

    glfwSetWindowMonitor(
        m_windowHandle,
        monitor,
        targetX,
        targetY,
        targetW,
        targetH,
        targetRefresh
    );

    m_windowMode = windowMode;

    LOG_INFO("Mode -> %s (%dx%d @ %dHz)",
        toString(windowMode), targetW, targetH, targetRefresh);
}

void WindowManager::updateInput() {
    // Cleared before the poll, because the scroll callback accumulates into it.
    m_inputHandle.getMouse().resetScrollDelta();

    glfwPollEvents();

    m_inputHandle.update(m_windowHandle);
}

bool WindowManager::beginFrame() {
    m_frameLimiter.beginFrame();

    return !shouldClose();
}

void WindowManager::setVSync(bool enabled) {
    // Independent of the software FPS cap: leave the framelimiter alone.
    if (!hasWindow("setVSync")) return;

    glfwMakeContextCurrent(m_windowHandle);
    // 0 = uncapped, 1 = vsync.
    glfwSwapInterval(enabled ? 1 : 0);
    m_vsync = enabled;
    LOG_INFO("VSync %s", enabled ? "ON" : "OFF");
}

void WindowManager::setFramerate(int framerate) {
    // Does not touch the swap interval: vsync is the independent knob, and with
    // both active the lower effective rate wins.
    m_frameLimiter.setTargetFramerate(framerate);
    if (framerate > 0) {
        LOG_INFO("FPS cap = %d", framerate);
    } else {
        LOG_INFO("FPS cap removed (unlimited)");
    }
}

void WindowManager::setCursorMode(CursorMode mode) {
    if (!hasWindow("setCursorMode")) return;

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
    glfwSetInputMode(m_windowHandle, GLFW_CURSOR, glfwmode);
}

size_t WindowManager::getWidth() const {
    if (!hasWindow("getWidth")) return 0;
    return m_width;
}
size_t WindowManager::getHeight() const {
    if (!hasWindow("getHeight")) return 0;
    return m_height;
}

void WindowManager::setSize(int width, int height) {
    m_width = width;
    m_height = height;
}

bool WindowManager::hasWindow(const char* action) const {
    if (m_windowHandle) return true;
    LOG_ERROR("%s: window is not initialized", action);
    return false;
}

int WindowManager::getRefreshRate() const {
    GLFWmonitor* monitor = glfwGetWindowMonitor(m_windowHandle);

    // Null when windowed - fall back to the primary monitor.
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

float WindowManager::framebufferScale() const {
    // Silent on a missing window: this runs per frame, and 1.0f is the honest
    // answer for the unscaled case a caller falls back to anyway.
    if (!m_windowHandle) return 1.0f;

    int windowWidth = 0;
    glfwGetWindowSize(m_windowHandle, &windowWidth, nullptr);
    if (windowWidth <= 0) return 1.0f;

    return static_cast<float>(m_width) / static_cast<float>(windowWidth);
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