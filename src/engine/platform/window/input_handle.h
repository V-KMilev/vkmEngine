#pragma once

struct GLFWwindow;

namespace Engine {

// https://www.glfw.org/docs/latest/group__buttons.html
#if !defined(GLFW_MOUSE_BUTTON_LAST)
    #define GLFW_MOUSE_BUTTON_LAST 7
#endif

#if !defined(GLFW_KEY_LAST)
    #define GLFW_KEY_LAST 348
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
         * @brief Snapshots the previous key state. Call before glfwPollEvents().
         *
         * Key state is updated via GLFW key callback, not polling.
         */
        void update();

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
        friend class InputHandle;
        void onKeyEvent(int key, bool pressed);

        bool m_keyState[GLFW_KEY_LAST + 1] = {};
        bool m_prevKeyState[GLFW_KEY_LAST + 1] = {};
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
         * @deprecated Use InputHandle::setupCallbacks() instead.
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
        friend class InputHandle;

        /**
         * @brief Set scroll values from GLFW callback.
         * @internal Called automatically by the scroll callback.
         * @param xOffset Scroll delta X from callback.
         * @param yOffset Scroll delta Y from callback.
         */
        void setScrollDelta(double xOffset, double yOffset);

    private:
        bool m_buttonState[GLFW_MOUSE_BUTTON_LAST + 1] = {};
        bool m_prevButtonState[GLFW_MOUSE_BUTTON_LAST + 1] = {};

        double m_x = 0.0;
        double m_y = 0.0;
        double m_deltaX = 0.0;
        double m_deltaY = 0.0;

        double m_scrollX = 0.0;
        double m_scrollY = 0.0;
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
         * @brief Sets up GLFW callbacks for keyboard and scroll input.
         *
         * Must be called once after window creation. Key state and scroll delta
         * are updated via callbacks during glfwPollEvents().
         * @param window Pointer to the GLFW window.
         */
        void setupCallbacks(GLFWwindow* window);

        /**
         * @brief Update input state from the GLFW window.
         *
         * Updates mouse state (cursor position, buttons). Keyboard state is
         * managed via callbacks. Call keyboard().update() before glfwPollEvents()
         * to snapshot the previous frame's state.
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