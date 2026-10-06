#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief The "New Project" dialog: a name, a parent directory, and Create.
 *
 * Makes a project the way `vkm new` does - from templates/default, or from the
 * project EditorState::newProjectTemplate names, such as a shipped example - then
 * opens it through EditorState::requestSceneAction so the unsaved-changes guard runs.
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
        /**
         * @brief Copy @p source to @p dest and stamp it for this engine.
         *
         * @param source The project to copy.
         * @param dest Directory to create; must not already exist non-empty.
         * @param error Set when the result is false.
         * @return true when the project is ready to open.
         */
        static bool create(
            const std::filesystem::path& source,
            const std::filesystem::path& dest,
            std::string& error
        );

    private:
        std::filesystem::path m_source;   ///< What Create copies.
        bool m_open = false;
        char m_nameBuffer[128] = {};
        char m_parentBuffer[512] = {};
};

} // namespace Vkm::Engine
