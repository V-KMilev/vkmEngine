#pragma once

#include <memory>

struct GLFWwindow;
struct GLFWmonitor;

class Window;
class InputHandle;

enum class WindowMode {
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
constexpr const char* toString(WindowMode type) {
    switch (type) {
        case WindowMode::NONE:       return "NONE";
        case WindowMode::FULLSCREEN: return "FULLSCREEN";
        case WindowMode::WINDOWED:   return "WINDOWED";
        default: return "UNKNOWN";
    }
}

GLFWmonitor* getCurrentMonitor(GLFWwindow* window);

class WindowManager {
    public:
        ~WindowManager();

        WindowManager(const WindowManager& other) = delete;
        WindowManager& operator=(const WindowManager& other) = delete;

        WindowManager(WindowManager && other) = delete;
        WindowManager& operator=(WindowManager && other) = delete;

    public:
        // Singleton accessor
        static WindowManager& get();

        void createWindow(const std::string& title);

        std::unique_ptr<InputHandle>& getInputHandle();
        const std::unique_ptr<InputHandle>& getInputHandle() const;

        bool shouldClose() const;

        bool requestClose();

        bool updateMode(WindowMode windowMode);

        bool updateInput();

        bool swapBuffers();

    private:
        WindowManager() = default;

    private:
        std::unique_ptr<Window> m_window;
        std::unique_ptr<InputHandle> m_inputHandle;
};
