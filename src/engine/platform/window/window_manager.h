#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct GLFWwindow;
struct GLFWmonitor;

namespace Engine {

class Window;
class InputHandle;
class FrameLimiter;

/**
 * @brief Enumerates supported window modes for the application window.
 *
 * WindowMode is used to specify how the application window should be displayed on the user's monitor.
 * 
 * - NONE:       No active window mode or undefined state; the window may not be visible or active.
 * - FULLSCREEN: The window occupies the entire screen, with no window borders or decorations; 
 *               typically used for immersive applications and games.
 * - WINDOWED:   The window operates within a resizable and movable container, allowing it to share the
 *               desktop with other applications; contains standard OS borders and controls.
 */
enum class WindowMode {
    None       = 0,    ///< No active window mode or undefined.
    Fullscreen = 1,    ///< Window occupies the entire screen.
    Windowed   = 2     ///< Window is in windowed mode.
};

/**
 * @brief Enumerates supported cursor modes for the application window.
 *
 * - NORMAL:   Standard visible cursor, can move outside the window.
 * - HIDDEN:   Cursor is invisible but movement is unrestricted.
 * - DISABLED: Cursor is hidden and movement is confined to the window (useful for FPS cameras).
 * - CAPTURED: Cursor is captured and not visible, often for raw input scenarios.
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
        case WindowMode::None:       return "None";
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
         * @brief Checks if the window close event has been triggered.
         * @return true if the window should close, false otherwise.
         */
        bool shouldClose() const;

        /// Alias for shouldClose(), used by the save-on-quit flow whose
        /// "wants to close" reads more naturally than "should close".
        bool wantsClose() const { return shouldClose(); }

        /**
         * @brief Requests that the window be closed.
         * @return true if the request was successful, false otherwise.
         */
        bool requestClose();

        /// Cancel a pending close. Used by the save-on-quit modal when the
        /// user picks Cancel (or Save - the close is deferred until the
        /// scene is clean). Keeps GLFW out of the editor.
        void cancelClose();

        /// Update the window title. Used by the editor to reflect the
        /// current scene's filename and dirty state without reaching for
        /// raw GLFW.
        void setTitle(const std::string& title);

        /**
         * @brief Swaps the front and back buffers of the window, presenting the rendered image.
         * @return true if the swap succeeded, false otherwise.
         */
        bool swapBuffers();

    public:
        /**
         * @brief Changes the current window mode (fullscreen or windowed).
         * @param windowMode The desired window mode.
         * @return true if the mode switch was successful, false otherwise.
         */
        bool updateMode(WindowMode windowMode);

        /**
         * @brief Updates all input states (keyboard, mouse, etc.).
         * @return true if the update was successful, false otherwise.
         */
        bool updateInput();

        /**
         * @brief Prepares rendering for the next frame.
         * @return true if frame start was successful, false otherwise.
         */
        bool beginFrame();

        /**
         * @brief Enables or disables vertical synchronization (VSync).
         * @param enabled True to enable VSync, false to disable.
         */
        void setVSync(bool enabled);

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
         * @brief Get the width of the window.
         * @return The width of the window.
         */
        size_t getWidth() const;
        /**
         * @brief Get the height of the window.
         * @return The height of the window.
         */
        size_t getHeight() const;

        /**
         * @brief Get the underlying GLFW window pointer.
         * @return Pointer to the GLFWwindow, or nullptr if not initialized.
         */
        GLFWwindow* getWindowContext() const;

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
        uint32_t sceneViewportWidth()  const { return m_sceneVpW; }
        uint32_t sceneViewportHeight() const { return m_sceneVpH; }

    private:
        std::unique_ptr<Window> m_window;
        std::unique_ptr<InputHandle> m_inputHandle;
        std::unique_ptr<FrameLimiter> m_frameLimiter;

        // Scene viewport rect inside the window (set by the editor, read
        // by the engine when populating FrameContext). 0 in width/height
        // means "follow the window".
        uint32_t m_sceneVpX = 0;
        uint32_t m_sceneVpY = 0;
        uint32_t m_sceneVpW = 0;
        uint32_t m_sceneVpH = 0;
};

} // namespace Engine