#pragma once

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief The "Open Project" dialog: a path field, refused until it names a project.
 *
 * The recent projects are File > Open Recent's and the start screen's.
 *
 * Drawn at the root window's scope for the same reason as ModelImportDialog.
 * Opens through EditorState::requestSceneAction so the unsaved-changes guard runs.
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
         * @brief Open the dialog when EditorState::requestOpenProject is set,
         *        and request whichever project the user chooses.
         *
         * @param state Receives the request.
         */
        void draw(EditorState& state);

    private:
        bool m_open = false;
        char m_pathBuffer[512] = {};
};

} // namespace Vkm::Engine
