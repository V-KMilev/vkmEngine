#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief Highest key and mouse-button codes this header sizes its arrays for.
 *
 * Engine-owned so input does not include GLFW; window_manager.cpp static_asserts them against it.
 */
constexpr int MAX_MOUSE_BUTTON = 7;    // GLFW_MOUSE_BUTTON_LAST
constexpr int MAX_KEY          = 348;  // GLFW_KEY_LAST

/**
 * @brief One frame of raw device state: keys and buttons down, cursor position and wheel turn.
 *
 * Source-agnostic, which keeps the windowing library out of the input path. Main thread only, so
 * unsynchronised. A key or button also records a strike since the last clearStrikes(): one poll
 * can deliver a press and its release together, which the level alone would miss.
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
         * @brief Check whether the specified key is currently held down.
         *
         * @param key The GLFW key code to query.
         * @return True if the key is currently pressed; false for out-of-range keys.
         */
        bool isKeyPressed(int key) const;

        /**
         * @brief Whether the key went down since the last clearStrikes().
         *
         * True even when it is up again: a tap shorter than one poll.
         *
         * @param key The GLFW key code to query.
         * @return True if a press arrived; false for out-of-range keys.
         */
        bool wasKeyStruck(int key) const;

        /**
         * @brief Whether the key went down or auto-repeated since the last clearStrikes().
         *
         * @param key The GLFW key code to query.
         * @return True if a press or a repeat arrived; false for out-of-range keys.
         */
        bool wasKeyTyped(int key) const;

        /**
         * @brief Record a key's held state.
         *
         * A key going down is also recorded as struck. Out-of-range codes are ignored.
         *
         * @param key     The GLFW key code that changed.
         * @param pressed True when now held. A press of a key already held is a repeat:
         *                typed, not struck.
         */
        void onKeyEvent(int key, bool pressed);

        /**
         * @brief Record a character the keyboard produced.
         *
         * @param codepoint A Unicode codepoint.
         */
        void onText(char32_t codepoint);

        /**
         * @brief The characters typed since the last clearStrikes(), in order, as UTF-32.
         *
         * Layout, Shift and dead keys already applied.
         *
         * @return The characters, oldest first.
         */
        const std::u32string& text() const { return m_text; }

        /**
         * @brief Move the cursor to @p x, @p y, recording the step it took.
         *
         * The delta is derived, so position and movement cannot disagree.
         *
         * @param x Cursor X in window pixels, from the left.
         * @param y Cursor Y in window pixels, from the top.
         */
        void moveTo(double x, double y);

        /**
         * @brief Record a mouse button's held state.
         *
         * A button going down is also recorded as struck, as for a key.
         *
         * @param button Button code; out of range is ignored.
         * @param pressed True while it is down.
         */
        void setButton(int button, bool pressed);

        /**
         * @brief Check if the specified mouse button is pressed.
         *
         * @param button The GLFW mouse button code.
         * @return True if button is pressed.
         */
        bool isButtonPressed(int button) const;

        /**
         * @brief Whether the button went down since the last clearStrikes(),
         *        even if it is up again.
         *
         * @param button The GLFW mouse button code.
         * @return True if a press arrived; false for out-of-range buttons.
         */
        bool wasButtonStruck(int button) const;

        /**
         * @brief Forget every key and button press, repeat and character recorded so far.
         */
        void clearStrikes();

        /**
         * @brief Zero the scroll delta before the window polls for this frame's.
         *
         * The scroll callback adds to it; without this one notch would turn every frame after.
         */
        void resetScrollDelta();

        /**
         * @brief Add to this frame's scroll, which resetScrollDelta() clears.
         *
         * Accumulated: a frame can carry several scroll events.
         *
         * @param yOffset Notches turned, positive away from the viewer.
         */
        void addScroll(double yOffset);

        /**
         * @brief The absolute cursor X position.
         *
         * @return Cursor X in window pixels, from the left.
         */
        double getX() const { return m_x; }

        /**
         * @brief The absolute cursor Y position.
         *
         * @return Cursor Y in window pixels, from the top.
         */
        double getY() const { return m_y; }

        /**
         * @brief The X movement the last moveTo recorded.
         *
         * @return Pixels moved along X.
         */
        double getDeltaX() const { return m_deltaX; }

        /**
         * @brief The Y movement the last moveTo recorded.
         *
         * @return Pixels moved along Y.
         */
        double getDeltaY() const { return m_deltaY; }

        /**
         * @brief The scroll accumulated since resetScrollDelta().
         *
         * @return Notches turned, positive away from the viewer.
         */
        double getScrollY() const { return m_scrollY; }

    private:
        bool m_keyState[MAX_KEY + 1]  = {};
        bool m_keyStruck[MAX_KEY + 1] = {};  ///< Went down since the last clearStrikes().
        bool m_keyTyped[MAX_KEY + 1]  = {};  ///< Went down or repeated since then.

        std::u32string m_text;  ///< Capacity reused.

        bool m_buttonState[MAX_MOUSE_BUTTON + 1]  = {};
        bool m_buttonStruck[MAX_MOUSE_BUTTON + 1] = {};  ///< Went down since the last clearStrikes().

        double m_x = 0.0;
        double m_y = 0.0;
        double m_deltaX = 0.0;
        double m_deltaY = 0.0;

        double m_scrollY = 0.0;
};

} // namespace Vkm::Engine
