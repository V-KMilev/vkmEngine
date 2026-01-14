#pragma once

#include <memory>

struct GLFWwindow;
struct GLFWmonitor;

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
    NONE       = 0,    ///< No active window mode or undefined.
    FULLSCREEN = 1,    ///< Window occupies the entire screen.
    WINDOWED   = 2     ///< Window is in windowed mode.
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
    NORMAL   = 0,    ///< Cursor visible, standard mode.
    HIDDEN   = 1,    ///< Cursor hidden.
    DISABLED = 2,    ///< Cursor disabled and locked to window.
    CAPTURED = 3     ///< Cursor captured for raw input.
};

/**
 * @brief Convert a WindowMode enum value to its string representation.
 * 
 * @param type The WindowMode to convert.
 * @return const char* String representation of the WindowMode.
 */
constexpr const char* toString(WindowMode type) {
    switch (type) {
        case WindowMode::NONE:       return "NONE";
        case WindowMode::FULLSCREEN: return "FULLSCREEN";
        case WindowMode::WINDOWED:   return "WINDOWED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Manages the application's window, input, and frame limiting.
 *
 * This singleton class encapsulates all logic related to window creation,
 * rendering context management, input handling, and frame rate limiting.
 * It ensures only one window exists and centralizes window-related functionality.
 */
class WindowManager {
    public:
        WindowManager(const WindowManager& other) = delete;
        WindowManager& operator=(const WindowManager& other) = delete;

        WindowManager(WindowManager && other) = delete;
        WindowManager& operator=(WindowManager && other) = delete;

    public:
        /**
         * @brief Gets the singleton instance of WindowManager.
         * @return Reference to the singleton WindowManager.
         */
        static WindowManager& get();

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

        /**
         * @brief Requests that the window be closed.
         * @return true if the request was successful, false otherwise.
         */
        bool requestClose();

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

    private:
        WindowManager() = default;
        ~WindowManager();

    private:
        std::unique_ptr<Window> m_window;
        std::unique_ptr<InputHandle> m_inputHandle;
        std::unique_ptr<FrameLimiter> m_frameLimiter;
};
