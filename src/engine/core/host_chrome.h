#pragma once

#include <cstdint>

namespace Vkm::Engine {
    class WindowManager;
}

namespace Vkm::Engine {

/**
 * @brief What an authoring host tells the engine about the frame it draws over.
 *
 * The editor is a frame around the engine's output, not a window beside it: it
 * lays panels out and leaves the scene a rect in the middle, and it takes the
 * pointer whenever the cursor is over one of those panels. Four systems need
 * those two answers - render and visibility size themselves to the rect, the
 * game UI hit-tests inside it, the camera controller stops flying while a panel
 * has the pointer - and none of them has any business knowing an editor exists.
 *
 * So the host states it once, here, and every reader takes it off the frame
 * context. A runtime states nothing at all: the defaults are the whole window
 * and nobody holding the pointer, which is what a shipped game wants.
 *
 * Written in the UI stage and read in the stages before it, so a reader sees
 * the rect the host laid out on the previous frame. That is the trade the
 * editor has always made - the alternative is laying the panels out twice - and
 * it shows only on a resize.
 */
class HostChrome {
    public:
        HostChrome()  = default;
        ~HostChrome() = default;

        // A plain value: it owns nothing, and the engine holds exactly one.
        HostChrome(const HostChrome& other) = default;
        HostChrome& operator=(const HostChrome& other) = default;

        HostChrome(HostChrome && other) = default;
        HostChrome& operator=(HostChrome && other) = default;

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

        /**
         * @brief Declare the rect inside the window the 3D scene renders into.
         *
         * @param x     Left edge in framebuffer pixels. A host working in window
         *              screen coordinates (an ImGui rect) multiplies by
         *              WindowManager::framebufferScale first.
         * @param y     Top edge, same units.
         * @param width Width in framebuffer pixels; zero means the whole window.
         * @param height Height in framebuffer pixels; zero means the whole window.
         */
        void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
            m_viewport = ViewportRect{x, y, width, height};
        }

        /**
         * @brief The declared rect, resolved against the window it sits in.
         *
         * A zero extent means "no host has said otherwise", so the window fills
         * in - which is why this needs the window rather than answering from its
         * own state. Keeping the fallback here rather than in each reader is the
         * point: three systems ask, and a game host answers none of them.
         *
         * @param window The window the scene is drawn into.
         * @return The viewport rect in framebuffer pixels, never zero-sized
         *         unless the window itself is.
         */
        ViewportRect viewport(const WindowManager& window) const;

        /**
         * @brief Declare that the host's own UI, not the scene, has the devices.
         *
         * Two answers rather than one because the two are genuinely independent:
         * a text field being typed into takes the keyboard while the pointer is
         * still over the viewport, and a panel under the cursor takes the
         * pointer while nothing is focused.
         *
         * @param pointer  Whether host UI is under the cursor or dragging.
         * @param keyboard Whether host UI is taking text input.
         */
        void setCapture(bool pointer, bool keyboard) {
            m_capturePointer  = pointer;
            m_captureKeyboard = keyboard;
        }

        /// @return Whether the host's UI owns the pointer this frame.
        bool capturesPointer() const { return m_capturePointer; }

        /// @return Whether the host's UI owns the keyboard this frame.
        bool capturesKeyboard() const { return m_captureKeyboard; }

    private:
        ViewportRect m_viewport;
        bool m_capturePointer  = false;
        bool m_captureKeyboard = false;
};

} // namespace Vkm::Engine
