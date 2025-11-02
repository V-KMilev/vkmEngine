#pragma once

#include <unordered_map>

struct GLFWwindow;

// https://www.glfw.org/docs/latest/group__buttons.html
#if !defined(GLFW_MOUSE_BUTTON_LAST)
    #define GLFW_MOUSE_BUTTON_LAST 7
#endif

class KeyboardInputHandle {
    public:
        KeyboardInputHandle() = default;
        ~KeyboardInputHandle() = default;

        KeyboardInputHandle(const KeyboardInputHandle& other) = delete;
        KeyboardInputHandle& operator=(const KeyboardInputHandle& other) = delete;

        KeyboardInputHandle(KeyboardInputHandle && other) = delete;
        KeyboardInputHandle& operator=(KeyboardInputHandle && other) = delete;

    public:
        void update(GLFWwindow* window);

        bool isKeyPressed(int key) const;
        bool isKeyReleased(int key) const;

    private:
        std::unordered_map<int, bool> m_keyState;
        std::unordered_map<int, bool> m_prevKeyState;
};

class MouseInputHandle {
    public:
        MouseInputHandle() = default;
        ~MouseInputHandle() = default;

        MouseInputHandle(const MouseInputHandle& other) = delete;
        MouseInputHandle& operator=(const MouseInputHandle& other) = delete;

        MouseInputHandle(MouseInputHandle && other) = delete;
        MouseInputHandle& operator=(MouseInputHandle && other) = delete;

    public:
        void update(GLFWwindow* window);

        bool isButtonPressed(int button) const;
        bool isButtonReleased(int button) const;

        double getX() const { return m_x; }
        double getY() const { return m_y; }
        double getDeltaX() const { return m_deltaX; }
        double getDeltaY() const { return m_deltaY; }

    private:
        bool m_buttonState[GLFW_MOUSE_BUTTON_LAST + 1] = {};
        bool m_prevButtonState[GLFW_MOUSE_BUTTON_LAST + 1] = {};

        double m_x = 0.0;
        double m_y = 0.0;
        double m_deltaX = 0.0;
        double m_deltaY = 0.0;
};

class InputHandle {
    public:
        InputHandle() = default;
        ~InputHandle() = default;

        InputHandle(const InputHandle& other) = delete;
        InputHandle& operator=(const InputHandle& other) = delete;

        InputHandle(InputHandle && other) = delete;
        InputHandle& operator=(InputHandle && other) = delete;

    public:
        void update(GLFWwindow* window);

        bool isPressed(int key) const;
        bool isReleased(int key) const;

        KeyboardInputHandle& keyboard() { return m_keyboardHandle; }
        MouseInputHandle& mouse() { return m_mouseHandle; }

    private:
        KeyboardInputHandle m_keyboardHandle;
        MouseInputHandle m_mouseHandle;
};