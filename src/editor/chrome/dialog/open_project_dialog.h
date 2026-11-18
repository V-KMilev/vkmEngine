#pragma once

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief The "Open Project" dialog: a path field, refused until it names a project.
 *
 * The recent projects are File > Open Recent's and the start screen's. Drawn at root window scope
 * (see ModelImportDialog); opens through SceneActions::request, so the unsaved-changes guard runs.
 */
class OpenProjectDialog {
    public:
        OpenProjectDialog() = default;
        ~OpenProjectDialog() = default;

        OpenProjectDialog(const OpenProjectDialog& other) = delete;
        OpenProjectDialog& operator=(const OpenProjectDialog& other) = delete;

        OpenProjectDialog(OpenProjectDialog && other) = delete;
        OpenProjectDialog& operator=(OpenProjectDialog && other) = delete;

    public:
        /**
         * @brief On Ask::OpenProject, open the dialog and request the project chosen.
         *
         * @param state Receives the request.
         */
        void draw(EditorState& state);

    private:
        bool m_open            = false;
        char m_pathBuffer[512] = {};
};

} // namespace Vkm::Engine
