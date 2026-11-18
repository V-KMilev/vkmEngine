#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "core/render_mode.h"
#include "ecs/entity.h"

namespace Vkm::Platform {
class WindowManager;
} // namespace Vkm::Platform

namespace Vkm::Engine {


/**
 * @brief What an authoring host tells the engine about the frame it draws over.
 *
 * The host frames the engine's output: panels around a scene rect, taking the
 * pointer over them; whether the world is open to edit; the effects it previews
 * while the world stands still; and what it has the frame display. Readers take it
 * off the frame context; unset, it is the whole window with nothing captured.
 *
 * Written in the Editor stage, so earlier stages see the previous frame's rect.
 */
struct HostChrome {
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

    /**
     * @brief What a host has the frame display: a debug view in place of the picture, and the
     *        world grid. A game's frame is the picture, with no grid.
     */
    struct Display {
        RenderMode mode{};  ///< The composite's output; RenderMode::Lit is the picture.

        // The world grid: each axis's line, and the plane of every two axes on.
        bool gridAxisX = false;
        bool gridAxisY = false;
        bool gridAxisZ = false;

        /**
         * @brief Whether the grid draws at all.
         *
         * @return True when any of its axes is on.
         */
        bool gridShown() const { return gridAxisX || gridAxisY || gridAxisZ; }
    };

    ViewportRect          viewport;                 ///< The rect the 3D scene renders into, in framebuffer
                                                    ///< pixels (WindowManager::framebufferScale); a zero
                                                    ///< extent is the whole window, as viewportIn() says.
    bool                  capturePointer  = false;  ///< The host's UI has the pointer: under the cursor,
                                                    ///< dragging, or the view taken from the game (an
                                                    ///< ejected session). A panel takes it unfocused.
    bool                  captureKeyboard = false;  ///< The host's UI has the keyboard: a field typed into,
                                                    ///< a popup or rebind waiting for a key, or the view
                                                    ///< taken from the game; even with the pointer outside.
    bool                  authoring       = false;  ///< The host may change the world this frame, so nothing
                                                    ///< is kept for a Mesh being fixed (Mesh::movable): the
                                                    ///< author moves fixed things too.
    EffectPreview         effectPreview;            ///< What plays while authoring, the world standing still;
                                                    ///< played so, an effect changes nothing of the scene.
    std::vector<uint32_t> hiddenInView;             ///< Slots, each subtree whole, whose meshes the authored
                                                    ///< world leaves out: the editor's eye on a Hierarchy
                                                    ///< row.
    bool                  coverNextWorld  = false;  ///< The next world to arrive is covered by the loading
                                                    ///< screen until ready, though authored: a project or
                                                    ///< scene opened. Set before the load; see
                                                    ///< takeCoverNextWorld().
    Display               display;                  ///< What the frame displays: a debug view, the grid.

    /**
     * @brief The declared rect, resolved against the window it sits in.
     *
     * @param window Window the scene is drawn into; fills a zero extent.
     * @return Rect in framebuffer pixels, zero-sized only if the window is.
     */
    ViewportRect viewportIn(const Platform::WindowManager& window) const;

    /**
     * @brief Whether coverNextWorld was set since this was last called; clears it.
     *
     * @return The request.
     */
    bool takeCoverNextWorld() { return std::exchange(coverNextWorld, false); }
};

} // namespace Vkm::Engine
