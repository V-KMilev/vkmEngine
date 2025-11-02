#pragma once

#include <string>
#include <memory>

// Forward declaration to avoid including <GLFW/glfw3.h> here
struct GLFWwindow;

// TOOD: Move this into conifg
#define OPENGL_MAJOR_VERSION 3
#define OPENGL_MINOR_VERSION 3

#define DEFAULT_WINDOW_WIDTH 1920
#define DEFAULT_WINDOW_HEIGHT 1080

class Window {
    public:
        Window() = delete;
        ~Window();

        Window(const Window& other) = delete;
        Window& operator=(const Window& other) = delete;

        Window(Window && other) = delete;
        Window& operator=(Window && other) = delete;

        Window(
            const std::string& title
        );

    public:
        int getWidth() const;
        int getHeight() const;
        int getRefreshRate() const;

        GLFWwindow* getWindowContext() const;

    private:
        void cleanup();

    private:
        std::string m_title;

        GLFWwindow* m_window;
};
