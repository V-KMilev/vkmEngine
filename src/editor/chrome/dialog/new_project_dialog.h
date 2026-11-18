#pragma once

#include <filesystem>
#include <string>

namespace Vkm::Engine {

struct EditorState;

/**
 * @brief The "New Project" dialog: a name, a parent directory, and Create.
 *
 * `vkm new` makes it from templates/default or the project an ask of NewProject names; it
 * opens through SceneActions::request, so the unsaved-changes guard runs.
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
         * @brief On an ask of NewProject, open the dialog and request the project created.
         *
         * @param state Carries the request; receives the open.
         */
        void draw(EditorState& state);

    private:
        std::filesystem::path m_source;  ///< What Create copies.
        bool                  m_open              = false;
        char                  m_nameBuffer[128]   = {};
        char                  m_parentBuffer[512] = {};
};

} // namespace Vkm::Engine
