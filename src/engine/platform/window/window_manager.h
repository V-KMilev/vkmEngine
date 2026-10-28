#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/reflect.h"
#include "platform/window/frame_limiter.h"
#include "platform/input/input_handle.h"
#include "platform/window/window_surface.h"

struct GLFWwindow;
struct GLFWmonitor;

namespace Vkm::Engine {

// Window-creation defaults.
inline constexpr int DEFAULT_WINDOW_WIDTH  = 1920;
inline constexpr int DEFAULT_WINDOW_HEIGHT = 1080;

/**
 * @brief Enumerates supported window modes for the application window.
 */
enum class WindowMode {
    Fullscreen,
    Windowed,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief A window's size in pixels.
 */
struct WindowSize {
    int width  = 0;
    int height = 0;

    bool operator==(const WindowSize& other) const { return width == other.width && height == other.height; }
};

/**
 * @brief The sizes of @p modes that fit on a monitor of @p monitor's size, largest first, each once.
 *
 * What a settings screen offers as render resolutions (WindowManager::renderResolutions).
 *
 * @param modes   A monitor's video modes, as GLFW lists them: any order, sizes repeated by rate.
 * @param monitor The monitor's own resolution.
 * @return The sizes no larger than it on either axis.
 */
std::vector<WindowSize> fittingWindowSizes(const std::vector<WindowSize>& modes, WindowSize monitor);

/**
 * @brief Cursor visibility and confinement for the application window.
 */
enum class CursorMode {
    Normal   = 0,    ///< Visible, free to leave the window.
    Hidden   = 1,    ///< Invisible, unrestricted.
    Disabled = 2,    ///< Hidden and confined, as an FPS camera wants.
    Captured = 3     ///< Visible but confined.
};

/**
 * @brief Owns the application's window, its WindowSurface, input, and frame limiting.
 *
 * **A closed window accepts every request and performs none**, and reporters answer zero
 * (framebufferScale answers one): a windowless world still ticks and must still be able to end.
 */
class WindowManager {
    public:
        WindowManager();
        ~WindowManager();

        WindowManager(const WindowManager& other) = delete;
        WindowManager& operator=(const WindowManager& other) = delete;

        WindowManager(WindowManager && other) = delete;
        WindowManager& operator=(WindowManager && other) = delete;

    public:
        /**
         * @brief Creates the main application window with the specified title.
         *
         * @param title   The window title.
         * @param surface What the graphics backend draws into it through (WindowSurface): hinted
         *                before the window exists, given it after, and kept to present each frame.
         * @throws std::runtime_error when there is no surface, or GLFW or the surface fails.
         */
        void createWindow(const std::string& title, std::unique_ptr<WindowSurface> surface);

        /**
         * @brief Sets the window/taskbar icon from an image file (PNG, etc.).
         *
         * A no-op before createWindow(); a file that fails to load is logged and skipped.
         *
         * @param path Absolute path to the icon image.
         */
        void setIcon(const std::string& path);

        /**
         * @brief Checks if the window close event has been triggered.
         *
         * @return true if the window should close, false otherwise.
         */
        bool shouldClose() const;

        /**
         * @brief Requests that the window be closed.
         */
        void requestClose();

        /**
         * @brief Cancel a pending close, e.g. behind an unsaved-changes guard.
         */
        void cancelClose();

        /**
         * @brief Update the window title.
         *
         * @param title The new title; kept even with no window.
         */
        void setTitle(const std::string& title);

        /**
         * @brief Present the rendered image through the window's surface, then keep the frame cap.
         */
        void swapBuffers();

        /**
         * @brief Write the next frame the game draws to @p pngPath, once.
         *
         * A request RenderSystem takes after its next frame: the game's view at viewport size,
         * without a host's panels. A second request replaces the first; a host drawing nothing
         * writes nothing.
         *
         * @param pngPath Where the PNG goes; the directory must exist.
         */
        void saveScreenshot(std::string pngPath) { m_screenshotPath = std::move(pngPath); }

        /**
         * @brief Hand over the pending screenshot request, leaving none.
         *
         * @return The path saveScreenshot named, or empty when none is pending.
         */
        std::string takeScreenshotRequest() { return std::exchange(m_screenshotPath, std::string()); }

        /**
         * @brief Changes the current window mode (fullscreen or windowed).
         *
         * @param windowMode The desired window mode.
         */
        void updateMode(WindowMode windowMode);

        /**
         * @brief The resolutions a settings screen can offer to render at: the monitor's own
         *        video modes that fit on it (fittingWindowSizes).
         *
         * The window stays the size it is; a chosen one becomes RenderSettings::renderScale
         * (RenderSettings::renderScaleFor), and the frame is scaled up to the window.
         *
         * @return Largest first; empty with no window.
         */
        std::vector<WindowSize> renderResolutions() const;

        /**
         * @brief Updates all input states (keyboard, mouse, etc.).
         */
        void updateInput();

        /**
         * @brief Answer the desktop's events, with no frame: for a long stretch before the first,
         *        so the window is not marked as hung.
         */
        void pumpEvents();

        /**
         * @brief Start the frame limiter's clock for the next frame.
         *
         * @return False once a close has been requested.
         */
        bool beginFrame();

        /**
         * @brief Enables or disables vertical synchronization (VSync).
         *
         * @param enabled True to enable VSync, false to disable.
         */
        void setVSync(bool enabled);

        /**
         * @brief Sets the maximum framerate for the render loop.
         *
         * @param framerate Frames per second limit.
         */
        void setFramerate(int framerate);

        /**
         * @brief Sets the cursor mode.
         *
         * Kept even with no window, so cursorMode() answers what it was told.
         *
         * @param mode The desired cursor mode.
         */
        void setCursorMode(CursorMode mode);

        /**
         * @brief Whether a window exists at all.
         *
         * Ask before reading a device: a reader cannot otherwise tell "zero" from "no display".
         *
         * @return True when this manager owns an open window.
         */
        bool isOpen() const { return m_windowHandle != nullptr; }

        /**
         * @brief The window mode last applied by updateMode.
         *
         * @return The last applied mode; Windowed at creation.
         */
        WindowMode mode() const { return m_windowMode; }

        /**
         * @brief Whether vsync was last enabled via setVSync.
         *
         * @return The last applied vsync state; off at creation.
         */
        bool vsync() const { return m_vsync; }

        /**
         * @brief The frame cap currently in effect.
         *
         * Independent of vsync; with both active the lower rate wins.
         *
         * @return Frames per second, or 0 when the rate is uncapped.
         */
        int framerate() const { return m_frameLimiter.targetFramerate(); }

        /**
         * @brief The cursor mode last set.
         *
         * @return CursorMode::Normal until something sets another.
         */
        CursorMode cursorMode() const { return m_cursorMode; }

        /**
         * @brief Get the current input handle for querying input state.
         *
         * @return The device state.
         */
        InputHandle& getInputHandle() { return m_inputHandle; }
        const InputHandle& getInputHandle() const { return m_inputHandle; }

        /**
         * @brief Keep the files dropped on the window for takeDroppedFiles(); off until a host that
         *        takes them asks, so one that never does keeps none.
         *
         * @param keep Whether to keep them.
         */
        void keepDroppedFiles(bool keep) { m_keepDropped = keep; }

        /**
         * @brief The files dropped on the window since the last call, as the system named them.
         *
         * @return Their paths, oldest drop first; taken, so a second call answers none.
         */
        std::vector<std::string> takeDroppedFiles() { return std::exchange(m_dropped, {}); }

        /**
         * @brief Get the framebuffer width in pixels.
         *
         * The drawable size, which differs from the window's screen coords on a HiDPI display.
         *
         * @return The framebuffer width in pixels.
         */
        size_t getWidth() const;

        /**
         * @brief Get the framebuffer height in pixels; see getWidth().
         *
         * @return The framebuffer height in pixels.
         */
        size_t getHeight() const;

        /**
         * @brief Framebuffer pixels per window screen coordinate.
         *
         * Diverges from 1 under HiDPI scaling, where GLFW reports window geometry and cursor in
         * screen coords; anything crossing to the drawable multiplies by this.
         *
         * @return The scale, or 1.0f when there is no window yet.
         */
        float framebufferScale() const;

        /**
         * @brief Get the underlying GLFW window pointer.
         *
         * @return The window, or nullptr if not initialized.
         */
        GLFWwindow* getWindowContext() const;

        /**
         * @brief Height in pixels of the monitor this window is on.
         *
         * The monitor, not the window, so the answer survives a maximise or fullscreen switch.
         *
         * @return Monitor height in pixels, or 0 when there is no window.
         */
        int displayHeight() const;

    private:
        /**
         * @brief Set the cached drawable dimensions, from the framebuffer-size callback.
         *
         * Unsynchronised: GLFW callbacks fire in glfwPollEvents() on the main thread.
         *
         * @param width New framebuffer width in pixels.
         * @param height New framebuffer height in pixels.
         */
        void setSize(int width, int height);

        /**
         * @brief The refresh rate of the monitor this window is on.
         *
         * @return Refresh rate in Hz, or 0 (logged) when there is no video mode.
         */
        int getRefreshRate() const;

    private:
        GLFWwindow*                    m_windowHandle = nullptr;
        std::unique_ptr<WindowSurface> m_surface;  ///< The backend's tie to the window; null headless.
        std::string m_title;

        /// Ours rather than GLFW's, so a windowless world can still be told to end.
        bool m_closeRequested = false;

        int m_width  = 0;    ///< Framebuffer pixels.
        int m_height = 0;

        int    m_windowWidth = 0;    ///< Screen coordinates, kept by callback.
        double m_cursorX     = 0.0;  ///< Screen coordinates, kept by callback.
        double m_cursorY     = 0.0;
        CursorMode m_cursorMode = CursorMode::Normal;

        InputHandle  m_inputHandle;
        FrameLimiter m_frameLimiter;

        WindowMode m_windowMode = WindowMode::Windowed;
        bool       m_vsync      = false;
        std::string m_screenshotPath;  ///< Pending until RenderSystem takes it.
        std::vector<std::string> m_dropped;              ///< Until takeDroppedFiles().
        bool                     m_keepDropped = false;  ///< Whether a host takes them.
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::WindowMode, "Fullscreen", "Windowed")
