#pragma once

#include <cstdint>

namespace Vkm::Engine {

class WindowManager;

/**
 * @brief What an authoring host tells the engine about the frame it draws over.
 *
 * The host frames the engine's output: panels around a scene rect, taking the
 * pointer over them. Readers take it off the frame context; unset, it is the
 * whole window with nothing captured.
 *
 * Written in the Editor stage, so earlier stages see the previous frame's rect.
 */
class HostChrome {
    public:
        /**
         * @brief A rect in framebuffer pixels: the unit glViewport takes.
         */
        struct ViewportRect {
            uint32_t x      = 0;
            uint32_t y      = 0;
            uint32_t width  = 0;
            uint32_t height = 0;
        };

    public:
        HostChrome()  = default;
        ~HostChrome() = default;

        HostChrome(const HostChrome& other) = default;
        HostChrome& operator=(const HostChrome& other) = default;

        HostChrome(HostChrome && other) = default;
        HostChrome& operator=(HostChrome && other) = default;

    public:
        /**
         * @brief Declare the rect inside the window the 3D scene renders into.
         *
         * @param x     Left edge in framebuffer pixels; scale screen coordinates
         *              by WindowManager::framebufferScale first.
         * @param y     Top edge, same units.
         * @param width Zero means the whole window.
         * @param height Zero means the whole window.
         */
        void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
            m_viewport = ViewportRect{x, y, width, height};
        }

        /**
         * @brief The declared rect, resolved against the window it sits in.
         *
         * @param window Window the scene is drawn into; fills a zero extent.
         * @return Rect in framebuffer pixels, zero-sized only if the window is.
         */
        ViewportRect viewport(const WindowManager& window) const;

        /**
         * @brief Declare that the host's own UI, not the scene, has the devices.
         *
         * Separate, since a focused text field takes the keyboard while the
         * pointer is over the viewport, and a panel takes the pointer unfocused.
         *
         * @param pointer  Host UI is under the cursor or dragging, or the host
         *                 has taken the view from the game (an ejected session).
         * @param keyboard A field is typed into, a popup or rebind waits for a
         *                 key, or the host has taken the view from the game.
         */
        void setCapture(bool pointer, bool keyboard) {
            m_capturePointer  = pointer;
            m_captureKeyboard = keyboard;
        }

        /**
         * @brief Whether the host's UI owns the pointer this frame.
         *
         * @return The last setCapture() pointer flag.
         */
        bool capturesPointer() const { return m_capturePointer; }

        /**
         * @brief Whether the host's UI owns the keyboard this frame.
         *
         * @return The last setCapture() keyboard flag.
         */
        bool capturesKeyboard() const { return m_captureKeyboard; }

    private:
        ViewportRect m_viewport;
        bool m_capturePointer  = false;
        bool m_captureKeyboard = false;
};

} // namespace Vkm::Engine
