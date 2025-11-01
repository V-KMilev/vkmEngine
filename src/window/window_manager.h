#pragma once

#include <memory>

class Window;

class WindowManager {
    public:
        WindowManager() = delete;
        ~WindowManager();

        WindowManager(const WindowManager& other) = delete;
        WindowManager& operator=(const WindowManager& other) = delete;

        WindowManager(WindowManager && other) = delete;
        WindowManager& operator=(WindowManager && other) = delete;

        WindowManager(std::unique_ptr<Window> && window) noexcept;

    public:

    private:
        std::unique_ptr<Window> m_window;
};
