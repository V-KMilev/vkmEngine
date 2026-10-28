#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {

class WindowManager;

/**
 * @brief What an authoring host tells the engine about the frame it draws over.
 *
 * The host frames the engine's output: panels around a scene rect, taking the
 * pointer over them; whether the world is open to edit; the effects it previews
 * while the world stands still. Readers take it off the frame context; unset, it
 * is the whole window with nothing captured.
 *
 * Written in the Editor stage, so earlier stages see the previous frame's rect.
 */
class HostChrome {
    public:
        /**
         * @brief A rect in framebuffer pixels, origin at the top left.
         */
        struct ViewportRect {
            uint32_t x      = 0;
            uint32_t y      = 0;
            uint32_t width  = 0;
            uint32_t height = 0;
        };

        /**
         * @brief What of the world's effects an authoring host plays while the world stands still.
         */
        struct EffectPreview {
            bool                  playing  = false;  ///< Whether to play them at all.
            bool                  paused   = false;  ///< Held where they are, still drawn.
            bool                  all      = false;  ///< Every emitter, not only those under `roots`.
            std::vector<EntityId> roots;             ///< Whose subtrees play: an editor's selection.
            uint64_t              restarts = 0;      ///< Moved to start them over from nothing.
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

        /**
         * @brief Declare whether the host may change the world this frame: an editor outside a
         *        running session, or with its panels shown.
         *
         * A world being authored keeps nothing between frames on the strength of a Mesh being
         * fixed (Mesh::movable): the author moves fixed things too.
         *
         * @param authoring Whether the world is open to the host's edits.
         */
        void setAuthoring(bool authoring) { m_authoring = authoring; }

        /**
         * @brief Whether the host may change the world this frame.
         *
         * @return The last setAuthoring() flag; false in a host that never sets it.
         */
        bool authoring() const { return m_authoring; }

        /**
         * @brief Ask for the world's particle effects to play while the world itself stands still.
         *
         * Read only while authoring(). An effect played so changes nothing of the scene: a burst is
         * not spent, a spent emitter is not destroyed, and one that has finished starts over.
         *
         * @param preview What plays.
         */
        void setEffectPreview(EffectPreview preview) { m_effects = std::move(preview); }

        /**
         * @brief What the host asked to play while it authors.
         *
         * @return The last setEffectPreview(); nothing plays in a host that never sets it.
         */
        const EffectPreview& effectPreview() const { return m_effects; }

        /**
         * @brief Ask for the next world to arrive to be covered by the loading screen until it is
         *        ready, though the host authors it: a project or scene it opens, not a world it
         *        puts back.
         *
         * Asked before the load; a load that fails takes it back (takeCoverNextWorld).
         */
        void coverNextWorld() { m_coverNext = true; }

        /**
         * @brief Whether coverNextWorld() was asked since this was last called; clears it.
         *
         * @return The request.
         */
        bool takeCoverNextWorld() { return std::exchange(m_coverNext, false); }

    private:
        EffectPreview m_effects;
        ViewportRect m_viewport;
        bool m_capturePointer  = false;
        bool m_captureKeyboard = false;
        bool m_authoring       = false;
        bool m_coverNext       = false;  ///< coverNextWorld() asked.
};

} // namespace Vkm::Engine
