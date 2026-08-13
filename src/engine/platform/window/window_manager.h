#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;
struct GLFWmonitor;

namespace Engine {

class InputHandle;
class FrameLimiter;

// Window-creation defaults. Platform/window-layer constants (WindowManager owns
// GL-context creation), kept here rather than in engine_config.h - that file
// is reserved for cross-cutting ECS / engine-loop limits, not backend/window
// knobs.
inline constexpr int OPENGL_MAJOR_VERSION  = 4;     ///< Requested GL context major version.
inline constexpr int OPENGL_MINOR_VERSION  = 3;     ///< Requested GL context minor version.
inline constexpr int OPENGL_GLSL_VERSION   = OPENGL_MAJOR_VERSION * 100 + OPENGL_MINOR_VERSION * 10;  ///< GLSL "#version" the shader loader injects (derived: 4.3 -> 430).
inline constexpr int DEFAULT_WINDOW_WIDTH  = 1920;  ///< Initial window width in pixels.
inline constexpr int DEFAULT_WINDOW_HEIGHT = 1080;  ///< Initial window height in pixels.

/**
 * @brief Enumerates supported window modes for the application window.
 *
 * WindowMode is used to specify how the application window should be displayed on the user's monitor.
 *
 * - Fullscreen: The window occupies the entire screen, with no window borders or decorations;
 *               typically used for immersive applications and games.
 * - Windowed:   The window operates within a resizable and movable container, allowing it to share the
 *               desktop with other applications; contains standard OS borders and controls.
 */
enum class WindowMode {
    Fullscreen = 1,    ///< Window occupies the entire screen.
    Windowed   = 2     ///< Window is in windowed mode.
};

/**
 * @brief Enumerates supported cursor modes for the application window.
 *
 * - Normal:   Standard visible cursor, can move outside the window.
 * - Hidden:   Cursor is invisible but movement is unrestricted.
 * - Disabled: Cursor is hidden and movement is confined to the window (useful for FPS cameras).
 * - Captured: Cursor is captured and not visible, often for raw input scenarios.
 */
enum class CursorMode {
    Normal   = 0,    ///< Cursor visible, standard mode.
    Hidden   = 1,    ///< Cursor hidden.
    Disabled = 2,    ///< Cursor disabled and locked to window.
    Captured = 3     ///< Cursor captured for raw input.
};

/**
 * @brief Convert a WindowMode enum value to its string representation.
 *
 * @param type The WindowMode to convert.
 * @return const char* String representation of the WindowMode.
 */
constexpr const char* toString(WindowMode type) {
    switch (type) {
        case WindowMode::Fullscreen: return "Fullscreen";
        case WindowMode::Windowed:   return "Windowed";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Owns the application's window, input, and frame limiting.
 *
 * Encapsulates window creation, rendering-context management, input handling,
 * and frame-rate limiting. Constructed and owned by the Engine - not a
 * singleton; the engine holds the single instance.
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
         * @param title The window title.
         */
        void createWindow(const std::string& title);

        /**
         * @brief Sets the window/taskbar icon from an image file (PNG, etc.).
         *
         * Decoded with stb_image to RGBA and handed to GLFW. No-op (with a log)
         * if the window is not yet created or the file fails to load. Call after
         * createWindow().
         *
         * @param path Absolute path to the icon image.
         */
        void setIcon(const std::string& path);

        /**
         * @brief Checks if the window close event has been triggered.
         * @return true if the window should close, false otherwise.
         */
        bool shouldClose() const;

        /**
         * @brief Requests that the window be closed.
         */
        void requestClose();

        /**
         * @brief Cancel a pending close. Used by the save-on-quit modal when the
         * user picks Cancel (or Save - the close is deferred until the
         * scene is clean). Keeps GLFW out of the editor.
         */
        void cancelClose();

        /**
         * @brief Update the window title. Used by the editor to reflect the
         * current scene's filename and dirty state without reaching for
         * raw GLFW.
         */
        void setTitle(const std::string& title);

        /**
         * @brief Swaps the front and back buffers of the window, presenting the rendered image.
         */
        void swapBuffers();

    public:
        /**
         * @brief Changes the current window mode (fullscreen or windowed).
         * @param windowMode The desired window mode.
         */
        void updateMode(WindowMode windowMode);

        /**
         * @brief The window mode last applied by updateMode (Windowed at
         * creation). Lets UI reflect the current state instead of guessing.
         */
        WindowMode mode() const { return m_windowMode; }

        /**
         * @brief Updates all input states (keyboard, mouse, etc.).
         */
        void updateInput();

        /**
         * @brief Prepares rendering for the next frame.
         * @return true while the window should stay open, false once a close
         * has been requested (drives the main loop's exit).
         */
        bool beginFrame();

        /**
         * @brief Enables or disables vertical synchronization (VSync).
         * @param enabled True to enable VSync, false to disable.
         */
        void setVSync(bool enabled);

        /**
         * @brief Whether vsync was last enabled via setVSync (off at creation).
         */
        bool vsync() const { return m_vsync; }

        /**
         * @brief Sets the maximum framerate for the render loop.
         * @param framerate The desired frames per second limit.
         */
        void setFramerate(int framerate);

        /**
         * @brief Sets the cursor mode.
         * @param mode The desired cursor mode.
         */
        void setCursorMode(CursorMode mode);

        /**
         * @brief Get the current input handle for querying input state.
         * @return Reference to the InputHandle.
         */
        InputHandle& getInputHandle() { return *m_inputHandle; }
        const InputHandle& getInputHandle() const { return *m_inputHandle; }

        /**
         * @brief Get the framebuffer width in pixels.
         *
         * This is the drawable size GL viewports and render targets are sized in,
         * which differs from the window size in screen coords on a HiDPI / scaled
         * display. Kept current by the framebuffer-size callback.
         *
         * @return The framebuffer width in pixels.
         */
        size_t getWidth() const;
        /**
         * @brief Get the framebuffer height in pixels.
         *
         * The drawable-height counterpart of getWidth(); see it for the
         * window-size-versus-framebuffer distinction.
         *
         * @return The framebuffer height in pixels.
         */
        size_t getHeight() const;

        /**
         * @brief Get the underlying GLFW window pointer.
         * @return Pointer to the GLFWwindow, or nullptr if not initialized.
         */
        GLFWwindow* getWindowContext() const;

        /**
         * @brief Set the cached drawable dimensions. Called from the GLFW
         * framebuffer-size callback.
         *
         * Thread safety: GLFW callbacks fire during glfwPollEvents() on the main
         * thread for single-window apps, so setSize/getWidth/getHeight are all
         * accessed from the same thread. No synchronization needed.
         *
         * @param width New framebuffer width in pixels.
         * @param height New framebuffer height in pixels.
         */
        void setSize(int width, int height);

        /**
         * @brief Describe the rect inside the window the 3D scene renders into.
         *
         * Defaults to the full window. The editor calls this each frame after
         * laying out its panels so the next frame's render system sees the
         * viewport rect and sizes its FBOs / projection accordingly.
         *
         * @param x,y Top-left of the viewport in window-pixel coords (ImGui-style).
         * @param w,h Viewport size in pixels. 0 falls back to "use the full window".
         */
        void setSceneViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
        uint32_t sceneViewportX()      const { return m_sceneVpX; }
        uint32_t sceneViewportY()      const { return m_sceneVpY; }
        uint32_t sceneViewportWidth()  const { return m_sceneVpW != 0 ? m_sceneVpW : static_cast<uint32_t>(m_width); }
        uint32_t sceneViewportHeight() const { return m_sceneVpH != 0 ? m_sceneVpH : static_cast<uint32_t>(m_height); }

    private:
        /**
         * @brief Returns the monitor's refresh rate (Hz) for this window.
         */
        int getRefreshRate() const;

        /**
         * @brief Guard for methods that need a live window: returns true if one
         * exists, else logs "<action>: window is not initialized" and returns
         * false so the caller can bail.
         *
         * @param action Caller name used in the log message.
         */
        bool hasWindow(const char* action) const;

    private:
        GLFWwindow* m_windowHandle = nullptr;
        std::string m_title;

        int m_width  = 0;    ///< Framebuffer (drawable) width in pixels.
        int m_height = 0;    ///< Framebuffer (drawable) height in pixels.

        std::unique_ptr<InputHandle> m_inputHandle;
        std::unique_ptr<FrameLimiter> m_frameLimiter;

        WindowMode m_windowMode = WindowMode::Windowed;  ///< Last applied mode.
        bool       m_vsync      = false;                 ///< Last applied vsync state.

        // Scene viewport rect inside the window (set by the editor, read
        // by the engine when populating FrameContext). 0 in width/height
        // means "follow the window".
        uint32_t m_sceneVpX = 0;
        uint32_t m_sceneVpY = 0;
        uint32_t m_sceneVpW = 0;
        uint32_t m_sceneVpH = 0;
};

} // namespace Engine
