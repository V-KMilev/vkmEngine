#pragma once

#include <cstddef>

namespace Vkm::Engine {

struct EditorContext;
struct EditorState;
class SceneIOController;

/**
 * @brief The editor's top menu bar.
 *
 * Drawn inside the root window's menu-bar scope. draw() also renders the scene
 * Save-As / Load dialogs so they stay in that scope; Import Model is
 * ModelImportDialog's, as its request is raised outside the menu bar too.
 */
class EditorMenuBar {
    public:
        EditorMenuBar() = default;
        ~EditorMenuBar() = default;

        EditorMenuBar(const EditorMenuBar& other) = delete;
        EditorMenuBar& operator=(const EditorMenuBar& other) = delete;

        EditorMenuBar(EditorMenuBar && other) = delete;
        EditorMenuBar& operator=(EditorMenuBar && other) = delete;

    public:
        void draw(EditorContext& ec, SceneIOController& sceneIO);

    private:
        /// Steps listed on each side of where the scene stands.
        static constexpr size_t HISTORY_SHOWN = 30;

    private:
        /**
         * @brief The Edit menu's selection items: cut, copy, paste, duplicate, delete, select all
         *        and deselect.
         *
         * @param ec Per-frame editor context.
         */
        void drawSelectionItems(EditorContext& ec);

        /**
         * @brief The Entity menu's placing items: align with the view or it with the entity, and drop
         *        to the ground.
         *
         * @param ec Per-frame editor context.
         */
        void drawPlacingItems(EditorContext& ec);

        void drawFileMenu(EditorContext& ec, SceneIOController& sceneIO);

        void drawEditMenu(EditorContext& ec);

        /**
         * @brief Edit > History: every step around where the scene stands, to walk to with a click.
         *
         * @param ec Supplies the history and the scene it walks.
         */
        void drawHistory(EditorContext& ec);

        /// Camera and viewport only: framing, and the debug overlays.
        void drawViewMenu(EditorContext& ec);

        void drawWindowMenu(EditorState& state);

        void drawEntityMenu(EditorContext& ec);

        /// Raises m_openAbout (see drawAboutPopup), or shows the shortcuts window.
        void drawHelpMenu();

        /**
         * @brief Every keybind as bound now, and the mouse's view controls, in one window.
         *
         * @param state Supplies the bindings.
         */
        void drawShortcutsWindow(EditorState& state);

        /**
         * @brief The About window.
         *
         * At menu-bar scope: OpenPopup inside a menu hashes its id against the
         * menu's popup window, so a Begin one scope out would never match.
         *
         * @param ec Per-frame editor context, for the backend it reports.
         */
        void drawAboutPopup(EditorContext& ec);

        /**
         * @brief The frame rate, right-aligned in the menu bar and colored by how it holds up.
         *
         * @param rate Frames a second.
         */
        static void drawFrameRate(float rate);

    private:
        bool m_openAbout     = false;  ///< About requested this frame.
        bool m_showShortcuts = false;  ///< The shortcuts window is open.
};

} // namespace Vkm::Engine
