#pragma once

#include <string>
#include <memory>

struct GLFWwindow;

namespace Engine {

// TODO: Move this into config
#define OPENGL_MAJOR_VERSION 4 ///< Default OpenGL major version.
#define OPENGL_MINOR_VERSION 3 ///< Default OpenGL minor version.

#define DEFAULT_WINDOW_WIDTH 1920  ///< Default window width in pixels.
#define DEFAULT_WINDOW_HEIGHT 1080 ///< Default window height in pixels.

/**
 * @class Window
 * @brief Represents an OS window and its context for rendering, input, and event handling.
 * 
 * Encapsulates window creation, management, and platform-related queries (such as size and refresh rate).
 * Non-copyable and non-movable, guaranteeing a single authoritative ownership of the window.
 */
class Window {
    public:
        Window() = delete;
        ~Window();

        Window(const Window& other) = delete;
        Window& operator=(const Window& other) = delete;

        Window(Window && other) = delete;
        Window& operator=(Window && other) = delete;

        /**
         * @brief Explicit constructor.
         * 
         * Creates a window with the specified title and sets the OpenGL swap interval.
         * 
         * @param title        The string displayed in the window's title bar.
         * @param swapInterval The swap interval ("vsync"); 0 disables vsync, >0 enables.
         */
        Window(
            const std::string& title,
            int swapInterval = 0
        );

    public:
        /**
         * @brief Returns the current width of the window in pixels.
         */
        int getWidth() const;

        /**
         * @brief Returns the current height of the window in pixels.
         */
        int getHeight() const;

        /**
         * @brief Returns the monitor's refresh rate (Hz) for this window.
         */
        int getRefreshRate() const;

        /**
         * @brief Returns the underlying native GLFWwindow pointer.
         * 
         * Enables integration with lower-level APIs if direct access is required.
         */
        GLFWwindow* getWindowContext() const;

        /**
         * @brief Sets the OpenGL swap interval (controls vsync on/off).
         * 
         * @param interval 0 to disable vsync, >0 to enable.
         */
        void setSwapInterval(int interval);

        /**
         * @brief Refresh cached width/height from GLFW. Call once per frame after glfwPollEvents.
         */
        void pollSize();

    private:
        /**
         * @brief Performs cleanup and resource release for the window.
         */
        void cleanup();

    private:
        std::string m_title;
        int m_swapInterval;

        GLFWwindow* m_window;
        int m_width  = 0;
        int m_height = 0;
};

} // namespace Engine