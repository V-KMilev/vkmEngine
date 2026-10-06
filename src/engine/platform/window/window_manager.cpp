#define VKM_LOG_CATEGORY "WINDOW"

#include "platform/window/window_manager.h"

#include <algorithm>
#include <stdexcept>

#include <GL/glew.h>
#include "platform/window/glfw_include.h"

#include "platform/input/input_handle.h"
#include "platform/window/frame_limiter.h"

#include "logger.h"

#include "debug/profiler.h"

#include "stb_image.h"

namespace Vkm::Engine {

static_assert(
    MAX_KEY          == GLFW_KEY_LAST,
    "MAX_KEY is out of step with this GLFW - resize the key arrays"
);
static_assert(
    MAX_MOUSE_BUTTON == GLFW_MOUSE_BUTTON_LAST,
    "MAX_MOUSE_BUTTON is out of step with this GLFW - resize the button arrays"
);

namespace {
// GLFW's per-thread error state, so a failed startup says what actually failed.
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
        // A display unplugged since the last poll still has a handle, but its mode is null.
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;

        int monitorX, monitorY;
        glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

        const int overlapRight  = std::min(windowX + windowWidth, monitorX + mode->width);
        const int overlapBottom = std::min(windowY + windowHeight, monitorY + mode->height);
        int overlapX = std::max(0, overlapRight - std::max(windowX, monitorX));
        int overlapY = std::max(0, overlapBottom - std::max(windowY, monitorY));
        int overlap = overlapX * overlapY;

        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            bestMonitor = monitors[i];
        }
    }

    return bestMonitor ? bestMonitor : glfwGetPrimaryMonitor();
}

/**
 * @brief The video mode of the monitor the window is on, or null if there isn't one.
 *
 * Found by overlap, as glfwGetWindowMonitor names one only when fullscreen. Null-safe for a
 * headless host or one with no output, since GLFW asserts on a null handle.
 */
const GLFWvidmode* currentVideoMode(GLFWwindow* window) {
    if (!window) return nullptr;
    GLFWmonitor* monitor = getCurrentMonitor(window);
    return monitor ? glfwGetVideoMode(monitor) : nullptr;
}
} // namespace

WindowManager::WindowManager() = default;

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
        // Usually no core context, but also no display or a missing platform; GLFW's text tells.
        LOG_ERROR(
            "Failed to create window (requested OpenGL %d.%d core): %s",
            OPENGL_MAJOR_VERSION,
            OPENGL_MINOR_VERSION,
            glfwErrorDescription()
        );
        throw std::runtime_error("Failed to create window");
    }

    // Required before glewInit and glfwSwapInterval below.
    glfwMakeContextCurrent(m_windowHandle);

    if (const GLenum glewError = glewInit(); glewError != GLEW_OK) {
        LOG_ERROR(
            "Failed to initialize GLEW: %s",
            reinterpret_cast<const char*>(glewGetErrorString(glewError))
        );
        throw std::runtime_error("Failed to initialize GLEW");
    }

    // VSync off at creation.
    glfwSwapInterval(0);

    glfwGetFramebufferSize(m_windowHandle, &m_width, &m_height);

    LOG_TRACE("Constructed Window '%s'", m_title.c_str());

    // Set before any callback that reads it is registered.
    glfwSetWindowUserPointer(m_windowHandle, this);

    // Framebuffer, not window, size: this also catches DPI changes.
    glfwSetFramebufferSizeCallback(m_windowHandle, [](GLFWwindow* w, int width, int height) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->setSize(width, height);
        }
    });

    // Kept by callback, not asked per frame: on X11 each question is a server round trip.
    glfwGetWindowSize(m_windowHandle, &m_windowWidth, nullptr);
    glfwSetWindowSizeCallback(m_windowHandle, [](GLFWwindow* w, int width, int) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->m_windowWidth = width;
        }
    });

    // The titlebar X, into the same field requestClose() writes.
    glfwSetWindowCloseCallback(m_windowHandle, [](GLFWwindow* w) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->requestClose();
        }
    });

    glfwSetKeyCallback(m_windowHandle, [](GLFWwindow* w, int key, int, int action, int) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            const bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
            manager->getInputHandle().onKeyEvent(key, pressed);
        }
    });

    // Characters with layout and modifiers applied, for text fields.
    glfwSetCharCallback(m_windowHandle, [](GLFWwindow* w, unsigned int codepoint) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->getInputHandle().onText(static_cast<char32_t>(codepoint));
        }
    });

    // An event, not a poll, so a click pressed and released within one poll is still seen.
    glfwSetMouseButtonCallback(m_windowHandle, [](GLFWwindow* w, int button, int action, int) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->getInputHandle().setButton(button, action == GLFW_PRESS);
        }
    });

    // Horizontal scroll is unused.
    glfwSetScrollCallback(m_windowHandle, [](GLFWwindow* w, double, double yOffset) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->getInputHandle().addScroll(yOffset);
        }
    });

    glfwSetCursorPosCallback(m_windowHandle, [](GLFWwindow* w, double x, double y) {
        if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w))) {
            manager->m_cursorX = x;
            manager->m_cursorY = y;
        }
    });

    // Leaving reports no position; the last one reported is still inside the window.
    glfwSetCursorEnterCallback(m_windowHandle, [](GLFWwindow* w, int entered) {
        auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (manager && !entered) glfwGetCursorPos(w, &manager->m_cursorX, &manager->m_cursorY);
    });

    // Seeds the delta's origin, or the first updateInput() reports the whole distance to the
    // pointer. updateInput overwrites the seed's own delta before anything reads it.
    glfwGetCursorPos(m_windowHandle, &m_cursorX, &m_cursorY);
    m_inputHandle.moveTo(m_cursorX, m_cursorY);

    LOG_INFO("Created window '%s' (%dx%d, refresh %dHz)", title.c_str(), m_width, m_height, getRefreshRate());
}

void WindowManager::setIcon(const std::string& path) {
    if (!m_windowHandle) return;
    int width, height, channels;

    // A loader may have set this thread's flip (see decodeImagesBottomUp); GLFWimage is top-left.
    stbi_set_flip_vertically_on_load_thread(0);

    // GLFWimage expects 32-bit RGBA.
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
    return m_closeRequested;
}

void WindowManager::requestClose() {
    LOG_INFO("Close requested");
    m_closeRequested = true;

    // Mirrored for GLFW readers; the loop reads the field, so a windowless world can stop.
    if (m_windowHandle) glfwSetWindowShouldClose(m_windowHandle, GLFW_TRUE);
}

void WindowManager::cancelClose() {
    LOG_VERBOSE("Pending close cancelled");
    m_closeRequested = false;
    if (m_windowHandle) glfwSetWindowShouldClose(m_windowHandle, GLFW_FALSE);
}

void WindowManager::setTitle(const std::string& title) {
    // Kept: the shutdown trace names m_title.
    m_title = title;
    if (m_windowHandle) glfwSetWindowTitle(m_windowHandle, m_title.c_str());
}

void WindowManager::swapBuffers() {
    {
        // A CPU outrunning the GPU blocks here: a fat zone means a GPU-bound frame.
        PROFILE_SCOPE("SwapBuffers");
        if (m_windowHandle) glfwSwapBuffers(m_windowHandle);
    }

    {
        // Its own zone, so a cap sleep is never mistaken for a swap stall.
        PROFILE_SCOPE("FrameLimiter");
        m_frameLimiter.endFrame();
    }
}

void WindowManager::updateMode(WindowMode windowMode) {
    // Headless: no window is not a monitor failure.
    if (!m_windowHandle) return;

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

    // Windowed is a centred 75% rect: a full-monitor one would look like borderless fullscreen.
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

    glfwSetWindowMonitor(m_windowHandle, monitor, targetX, targetY, targetW, targetH, targetRefresh);

    m_windowMode = windowMode;

    LOG_INFO("Mode -> %s (%dx%d @ %dHz)", toString(windowMode), targetW, targetH, targetRefresh);
}

void WindowManager::updateInput() {
    if (!m_windowHandle) return;

    // The scroll callback accumulates.
    m_inputHandle.resetScrollDelta();

    glfwPollEvents();

    // Once per frame: the handle's delta is the frame's movement.
    m_inputHandle.moveTo(m_cursorX, m_cursorY);
}

bool WindowManager::beginFrame() {
    m_frameLimiter.beginFrame();

    return !shouldClose();
}

void WindowManager::setVSync(bool enabled) {
    // Independent of the software FPS cap.
    if (!m_windowHandle) return;

    glfwMakeContextCurrent(m_windowHandle);
    glfwSwapInterval(enabled ? 1 : 0);
    m_vsync = enabled;
    LOG_INFO("VSync %s", enabled ? "ON" : "OFF");
}

void WindowManager::setFramerate(int framerate) {
    m_frameLimiter.setTargetFramerate(framerate);
    if (framerate > 0) {
        LOG_INFO("FPS cap = %d", framerate);
    } else {
        LOG_INFO("FPS cap removed (unlimited)");
    }
}

void WindowManager::setCursorMode(CursorMode mode) {
    m_cursorMode = mode;
    if (!m_windowHandle) return;

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

    // Entering and leaving Disabled warps the cursor, which GLFW reports no motion for.
    glfwGetCursorPos(m_windowHandle, &m_cursorX, &m_cursorY);
}

size_t WindowManager::getWidth() const {
    if (!m_windowHandle) return 0;
    return m_width;
}

size_t WindowManager::getHeight() const {
    if (!m_windowHandle) return 0;
    return m_height;
}

void WindowManager::setSize(int width, int height) {
    m_width = width;
    m_height = height;
}

int WindowManager::displayHeight() const {
    const GLFWvidmode* mode = currentVideoMode(m_windowHandle);
    return mode ? mode->height : 0;
}

int WindowManager::getRefreshRate() const {
    const GLFWvidmode* mode = currentVideoMode(m_windowHandle);
    if (!mode) {
        LOG_ERROR("Failed to get video mode for current monitor");
        return 0;
    }
    return mode->refreshRate;
}

float WindowManager::framebufferScale() const {
    // Silent: asked every frame, and 1.0f is what a caller falls back to anyway.
    if (!m_windowHandle) return 1.0f;

    if (m_windowWidth <= 0) return 1.0f;

    return static_cast<float>(m_width) / static_cast<float>(m_windowWidth);
}

GLFWwindow* WindowManager::getWindowContext() const {
    return m_windowHandle;
}

} // namespace Vkm::Engine
