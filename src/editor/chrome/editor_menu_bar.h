#pragma once

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
        /**
         * @brief The duplicate / delete / deselect items.
         *
         * @param ec Per-frame editor context.
         */
        void drawSelectionItems(EditorContext& ec);

        void drawFileMenu(EditorContext& ec, SceneIOController& sceneIO);

        void drawEditMenu(EditorContext& ec);

        /// Camera and viewport only: framing, and the debug overlays.
        void drawViewMenu(EditorContext& ec);

        void drawWindowMenu(EditorState& state);

        void drawEntityMenu(EditorContext& ec);

        /// Only raises m_openAbout; see drawAboutPopup.
        void drawHelpMenu();

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
         * @brief The frame rate, right-aligned in the menu bar and coloured by how it holds up.
         *
         * @param rate Frames a second.
         */
        static void drawFrameRate(float rate);

    private:
        bool m_openAbout = false;  ///< About requested this frame.
};

} // namespace Vkm::Engine
