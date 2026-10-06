#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief The "New Project" dialog: a name, a parent directory, and Create.
 *
 * Asks `vkm new` for it - from templates/default, or from the project
 * EditorState::newProjectTemplate names, such as a shipped example - and opens it
 * once made, through EditorState::requestSceneAction so the unsaved-changes guard runs.
 */
class NewProjectDialog {
    public:
        NewProjectDialog() = default;
        ~NewProjectDialog() = default;

        NewProjectDialog(const NewProjectDialog& other) = delete;
        NewProjectDialog& operator=(const NewProjectDialog& other) = delete;

        NewProjectDialog(NewProjectDialog && other) = delete;
        NewProjectDialog& operator=(NewProjectDialog && other) = delete;

    public:
        /**
         * @brief Open the dialog when EditorState::requestNewProject is set, and
         *        request whichever project the author creates.
         *
         * @param state Carries the request; receives the open.
         */
        void draw(EditorState& state);

    private:
        std::filesystem::path m_source;   ///< What Create copies.
        bool m_open = false;
        char m_nameBuffer[128] = {};
        char m_parentBuffer[512] = {};
};

} // namespace Vkm::Engine
