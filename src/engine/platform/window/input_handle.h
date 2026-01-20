#pragma once

#include <unordered_map>

struct GLFWwindow;

namespace Engine {

// https://www.glfw.org/docs/latest/group__buttons.html
#if !defined(GLFW_MOUSE_BUTTON_LAST)
    #define GLFW_MOUSE_BUTTON_LAST 7
#endif

/**
 * @brief Handles keyboard input state tracking and querying.
 * 
 * Provides methods to update and query the key states, 
 * including detecting if a key is pressed or released in the current frame.
 */
class KeyboardInputHandle {
    public:
        KeyboardInputHandle() = default;
        ~KeyboardInputHandle() = default;

        KeyboardInputHandle(const KeyboardInputHandle& other) = delete;
        KeyboardInputHandle& operator=(const KeyboardInputHandle& other) = delete;

        KeyboardInputHandle(KeyboardInputHandle && other) = delete;
        KeyboardInputHandle& operator=(KeyboardInputHandle && other) = delete;

    public:
        /**
         * @brief Update the key states from the provided GLFW window.
         * @param window Pointer to the GLFW window to query key input from.
         */
        void update(GLFWwindow* window);

        /**
         * @brief Check if the specified key is pressed this frame (edge or hold).
         * @param key The GLFW key code to query.
         * @return True if the key is currently pressed.
         */
        bool isKeyPressed(int key) const;

        /**
         * @brief Check if the specified key is released this frame.
         * @param key The GLFW key code to query.
         * @return True if the key was released.
         */
        bool isKeyReleased(int key) const;

    private:
        std::unordered_map<int, bool> m_keyState;
        std::unordered_map<int, bool> m_prevKeyState;
};

/**
 * @brief Handles mouse input state tracking and querying.
 * 
 * Provides methods to update and query the mouse button states, position,
 * movement (delta), and scroll. Also enables scroll callback setup.
 */
class MouseInputHandle {
    public:
        MouseInputHandle() = default;
        ~MouseInputHandle() = default;

        MouseInputHandle(const MouseInputHandle& other) = delete;
        MouseInputHandle& operator=(const MouseInputHandle& other) = delete;

        MouseInputHandle(MouseInputHandle && other) = delete;
        MouseInputHandle& operator=(MouseInputHandle && other) = delete;

    public:
        /**
         * @brief Update mouse state (position, button, etc.) from GLFW.
         * @param window Pointer to the GLFW window to query mouse input from.
         */
        void update(GLFWwindow* window);

        /**
         * @brief Setup scroll callback for this mouse handle with GLFW.
         * @param window Pointer to the GLFW window.
         * @param inputHandle Pointer to the input handle that owns this mouse handle.
         */
        void setupScrollCallback(GLFWwindow* window, class InputHandle* inputHandle);

        /**
         * @brief Check if the specified mouse button is pressed.
         * @param button The GLFW mouse button code.
         * @return True if button is pressed.
         */
        bool isButtonPressed(int button) const;

        /**
         * @brief Check if the specified mouse button is released.
         * @param button The GLFW mouse button code.
         * @return True if button is released.
         */
        bool isButtonReleased(int button) const;

        /**
         * @brief Returns current cursor X position in window.
         */
        double getX() const { return m_x; }

        /**
         * @brief Returns current cursor Y position in window.
         */
        double getY() const { return m_y; }

        /**
         * @brief Returns X movement delta (relative to last update).
         */
        double getDeltaX() const { return m_deltaX; }

        /**
         * @brief Returns Y movement delta (relative to last update).
         */
        double getDeltaY() const { return m_deltaY; }

        /**
         * @brief Returns scroll delta in X direction for this frame.
         */
        double getScrollX() const { return m_scrollX; }

        /**
         * @brief Returns scroll delta in Y direction for this frame.
         */
        double getScrollY() const { return m_scrollY; }

        /**
         * @brief Reset scroll delta at end of frame.
         * 
         * Must be called after processing input to prevent accumulation.
         */
        void resetScrollDelta();

    private:
        /**
         * @brief Set scroll values from GLFW callback.
         * @internal Called automatically by the scroll callback.
         * @param xOffset Scroll delta X from callback.
         * @param yOffset Scroll delta Y from callback.
         */
        void setScrollDelta(double xOffset, double yOffset);

    private:
        bool m_buttonState[GLFW_MOUSE_BUTTON_LAST + 1];
        bool m_prevButtonState[GLFW_MOUSE_BUTTON_LAST + 1];

        double m_x;
        double m_y;
        double m_deltaX;
        double m_deltaY;

        double m_scrollX;
        double m_scrollY;
};

/**
 * @brief Aggregates keyboard and mouse input handles for unified input state and queries.
 *
 * Provides update and high-level press/release queries for both keyboard and mouse.
 */
class InputHandle {
    public:
        InputHandle() = default;
        ~InputHandle() = default;

        InputHandle(const InputHandle& other) = delete;
        InputHandle& operator=(const InputHandle& other) = delete;

        InputHandle(InputHandle && other) = delete;
        InputHandle& operator=(InputHandle && other) = delete;

    public:
        /**
         * @brief Update input state for both keyboard and mouse from the GLFW window.
         * @param window Pointer to the GLFW window to query input from.
         */
        void update(GLFWwindow* window);

        /**
         * @brief Query if a key (keyboard or mouse) is pressed.
         * @param key Key code (interpreted as either keyboard or mouse, depending on context).
         * @return True if pressed.
         */
        bool isPressed(int key) const;

        /**
         * @brief Query if a key (keyboard or mouse) is released.
         * @param key Key code (interpreted as either keyboard or mouse, depending on context).
         * @return True if released.
         */
        bool isReleased(int key) const;

        /**
         * @brief Returns const reference to the keyboard input handle.
         */
        const KeyboardInputHandle& keyboard() const { return m_keyboardHandle; }

        /**
         * @brief Returns const reference to the mouse input handle.
         */
        const MouseInputHandle& mouse() const { return m_mouseHandle; }

        /**
         * @brief Returns mutable reference to the keyboard input handle.
         */
        KeyboardInputHandle& keyboard() { return m_keyboardHandle; }

        /**
         * @brief Returns mutable reference to the mouse input handle.
         */
        MouseInputHandle& mouse() { return m_mouseHandle; }

    private:
        KeyboardInputHandle m_keyboardHandle;
        MouseInputHandle m_mouseHandle;
};

} // namespace Engine