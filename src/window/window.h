#pragma once

#include <string>
#include <memory>

// Forward declaration to avoid including <GLFW/glfw3.h> here
struct GLFWwindow;
struct GLFWmonitor;

// TOOD: Move this into conifg
#define OPENGL_MAJOR_VERSION 3
#define OPENGL_MINOR_VERSION 3

enum WindowMode {
    NONE       = 0,
    FULLSCREEN = 1,
    WINDOWED   = 2
};

/**
 * @brief Convert an EntityType enum value to its string representation.
 * 
 * @param type The EntityType to convert.
 * @return const char* String representation of the EntityType.
 */
inline const char* toString(WindowMode type) {
    switch (type) {
        case WindowMode::NONE: return "NONE";
        case WindowMode::FULLSCREEN: return "FULLSCREEN";
        case WindowMode::WINDOWED: return "WINDOWED";
        default: return "UNKNOWN";
    }
}

GLFWmonitor* getCurrentMonitor(GLFWwindow* window);

class Window {
    public:
        Window() = delete;
        ~Window();

        Window(const Window& other) = delete;
        Window& operator=(const Window& other) = delete;

        Window(Window && other) = delete;
        Window& operator=(Window && other) = delete;

        Window(
            const std::string& title,
            int width,
            int height
        );

    public:
        int getWidth() const;

        int getHeight() const;

        int getRefreshRate() const;

        void setWindowMode(WindowMode windowMode);

        bool shouldClose() const;

        bool updateMode();

        bool updateWidthHeightOnResize();

        bool swapBuffers();

    private:
        void cleanup();

    private:
        GLFWwindow* m_window;

        std::string m_title;

        int m_width;
        int m_height;
        int m_refreshRate;

        WindowMode m_windowMode;
};

/*
I want the base of the engine to be the event system because it is the key to a smooth and consistent experience for the application. The idea that's been going around in my head is to have the event system as a global singleton entity that accepts tasks or functions to execute.

For example, if we have an interface input handler class that manages keyboard or mouse input, it can use the event system to pass functions that should be executed when a key is pressed or released. Within that class, we could also have functions that query input states—like an object class checking if a key is pressed—and then add the corresponding function to be executed by the event system.

The goal is to have a centralized event system that executes tasks in a particular order (e.g., through a priority queue) while maintaining full control over when, what, and how things are executed, and for what reason. I might be wrong about the design, but I believe this is the best approach to handle such logic in a render engine.
*/
