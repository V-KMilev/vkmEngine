#pragma once

struct GLFWwindow;

namespace Engine {

class WindowManager;

/**
 * @brief Bundles pointers needed by GLFW callbacks.
 *
 * Stored as the GLFW user pointer so all callbacks can access both
 * InputHandle (for input events) and WindowManager (for resize events).
 */
struct WindowCallbackData {
    class InputHandle* input = nullptr;
    WindowManager* window = nullptr;
};

// https://www.glfw.org/docs/latest/group__buttons.html
#if !defined(GLFW_MOUSE_BUTTON_LAST)
    #define GLFW_MOUSE_BUTTON_LAST 7
#endif

#if !defined(GLFW_KEY_LAST)
    #define GLFW_KEY_LAST 348
#endif

/**
 * @brief Tracks keyboard key state and answers per-key pressed queries.
 *
 * Key state is callback-driven (onKeyEvent from the GLFW key callback), so
 * there is no per-frame polling step.
 *
 * Thread safety: m_keyState is written from GLFW key callbacks which fire
 * during glfwPollEvents() on the main thread. All reads also happen on the
 * main thread. No synchronization needed for single-window apps.
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
         * @brief Check whether the specified key is currently held down.
         *
         * Reports level state, not edges: true for as long as the key is held.
         * @param key The GLFW key code to query.
         * @return True if the key is currently pressed; false for out-of-range keys.
         */
        bool isKeyPressed(int key) const;

    private:
        friend class InputHandle;
        /** @brief Record a key's held state; called from the GLFW key callback. */
        void onKeyEvent(int key, bool pressed);

        bool m_keyState[GLFW_KEY_LAST + 1] = {};
};

/**
 * @brief Handles mouse input state tracking and querying.
 *
 * Provides methods to update and query the mouse button states, position,
 * movement (delta), and scroll. Scroll is accumulated via the GLFW scroll
 * callback wired up by InputHandle, not by this class directly.
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
         * @brief Check if the specified mouse button is pressed.
         * @param button The GLFW mouse button code.
         * @return True if button is pressed.
         */
        bool isButtonPressed(int button) const;

        /**
         * @brief Returns the absolute cursor X position in window pixels.
         */
        double getX() const { return m_x; }

        /**
         * @brief Returns the absolute cursor Y position in window pixels.
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
         * @brief Accumulate scroll from the GLFW callback (Y only; X unused).
         * @internal Called automatically by the scroll callback.
         * @param yOffset Scroll delta Y from callback.
         */
        void setScrollDelta(double yOffset);

    private:
        bool m_buttonState[GLFW_MOUSE_BUTTON_LAST + 1] = {};

        double m_x = 0.0;
        double m_y = 0.0;
        double m_deltaX = 0.0;
        double m_deltaY = 0.0;

        double m_scrollY = 0.0;
};

/**
 * @brief Aggregates keyboard and mouse input handles for unified input state and queries.
 *
 * Provides update and high-level pressed queries for both keyboard and mouse.
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
         * @brief Sets up GLFW callbacks for keyboard, scroll, and window resize.
         *
         * Must be called once after window creation. Key state and scroll delta
         * are updated via callbacks during glfwPollEvents(); resize forwards to
         * WindowManager::setSize. Stores both pointers as the GLFW user pointer.
         * @param window        Pointer to the GLFW window.
         * @param windowManager Owning Engine::WindowManager, notified on resize.
         */
        void setupCallbacks(GLFWwindow* window, WindowManager* windowManager);

        /**
         * @brief Update input state from the GLFW window.
         *
         * Updates mouse state (cursor position, buttons). Keyboard state is
         * managed via callbacks, so it needs no per-frame polling step.
         * @param window Pointer to the GLFW window to query input from.
         */
        void update(GLFWwindow* window);

        /**
         * @brief Returns const reference to the keyboard input handle.
         */
        const KeyboardInputHandle& getKeyboard() const { return m_keyboardHandle; }

        /**
         * @brief Returns const reference to the mouse input handle.
         */
        const MouseInputHandle& getMouse() const { return m_mouseHandle; }

        /**
         * @brief Returns mutable reference to the keyboard input handle.
         */
        KeyboardInputHandle& getKeyboard() { return m_keyboardHandle; }

        /**
         * @brief Returns mutable reference to the mouse input handle.
         */
        MouseInputHandle& getMouse() { return m_mouseHandle; }

    private:
        KeyboardInputHandle m_keyboardHandle;
        MouseInputHandle m_mouseHandle;
        WindowCallbackData m_callbackData;
};

} // namespace Engine
