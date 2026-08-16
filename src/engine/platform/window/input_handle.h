#pragma once

struct GLFWwindow;

namespace Engine {

/**
 * @brief Highest key and mouse-button codes this header sizes its arrays for.
 *
 * Engine-owned constants rather than GLFW's macros, so that including
 * <GLFW/glfw3.h> is not forced on everything that touches input. Defining
 * GLFW's own macro names here would be worse than it looks: whichever header a
 * translation unit saw first would win, so the arrays below - and therefore
 * sizeof(KeyboardInputHandle) - would depend on include order. That is an ODR
 * violation, currently invisible only because the values happen to agree.
 *
 * input_handle.cpp does include GLFW and static_asserts these against the real
 * ones, so a GLFW that adds a key breaks the build instead of the memory.
 */
constexpr int MAX_MOUSE_BUTTON = 7;    // GLFW_MOUSE_BUTTON_LAST
constexpr int MAX_KEY          = 348;  // GLFW_KEY_LAST

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
        /**
         * @brief Record a key's held state.
         *
         * Called from the GLFW key callback during glfwPollEvents(); out-of-range
         * key codes are ignored.
         *
         * @param key     The GLFW key code that changed.
         * @param pressed True when the key is now held, false when released.
         */
        void onKeyEvent(int key, bool pressed);

        bool m_keyState[MAX_KEY + 1] = {};
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
        bool m_buttonState[MAX_MOUSE_BUTTON + 1] = {};

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
         * @brief Sets up the GLFW keyboard and scroll callbacks.
         *
         * Must be called once after window creation, and only once the owning
         * WindowManager is the window's GLFW user pointer - the capture-free
         * callbacks reach this handle back through it. Key state and scroll
         * delta are then updated during glfwPollEvents().
         *
         * @param window Pointer to the GLFW window.
         */
        void setupCallbacks(GLFWwindow* window);

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
};

} // namespace Engine
