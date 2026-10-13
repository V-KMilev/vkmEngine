#pragma once

#include <string>

struct GLFWwindow;

namespace Vkm::Engine {

/**
 * @brief What a graphics backend asks of the window it draws into: the window's one tie to an API.
 *
 * The window needs to know its surface before it exists - a context of some version, or none - and
 * hands the window over once it does; presenting a frame and the swap interval are the API's too.
 * A backend implements this beside its RenderBackend, and a host hands it to
 * WindowManager::createWindow, so nothing in the engine names an API.
 */
class WindowSurface {
    public:
        WindowSurface()          = default;
        virtual ~WindowSurface() = default;

        WindowSurface(const WindowSurface& other) = delete;
        WindowSurface& operator=(const WindowSurface& other) = delete;

        WindowSurface(WindowSurface && other) = delete;
        WindowSurface& operator=(WindowSurface && other) = delete;

    public:
        /**
         * @brief Ask GLFW for this surface, after glfwInit and before the window is created.
         */
        virtual void hint() const = 0;

        /**
         * @brief What hint() asks for, for a log line when the window cannot be made.
         *
         * @return The API and version the hints ask for.
         */
        virtual std::string describe() const = 0;

        /**
         * @brief Take the window it was hinted for and load the API on it, with vsync off.
         *
         * @param window The new window.
         * @throws std::runtime_error when the API cannot be loaded on it.
         */
        virtual void attach(GLFWwindow* window) = 0;

        /**
         * @brief Show the frame drawn into @p window.
         *
         * @param window The window attach() took.
         */
        virtual void present(GLFWwindow* window) = 0;

        /**
         * @brief Wait for the display's refresh before presenting, or not.
         *
         * @param window  The window attach() took.
         * @param enabled Whether to wait.
         */
        virtual void setVSync(GLFWwindow* window, bool enabled) = 0;
};

} // namespace Vkm::Engine
