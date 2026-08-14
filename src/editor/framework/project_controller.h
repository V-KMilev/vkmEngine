#pragma once

#include <string>

namespace Engine {

struct EditorContext;
class ScriptModule;

/**
 * @brief Opening a project from the editor, and remembering the ones opened.
 *
 * A project is the unit the editor edits, so switching one means re-rooting
 * everything scoped to it: the paths, the asset library, the gameplay module
 * and the scene. That ordering is the whole content of this class - each step
 * depends on the one before, and doing them out of order leaves the editor
 * showing one project's scene with another's assets.
 *
 * Draws its own dialog (recent projects plus a path field) and drives the
 * switch when one is chosen.
 */
class ProjectController {
    public:
        ProjectController() = default;
        ~ProjectController() = default;

        ProjectController(const ProjectController& other) = delete;
        ProjectController& operator=(const ProjectController& other) = delete;

        ProjectController(ProjectController && other) = delete;
        ProjectController& operator=(ProjectController && other) = delete;

    public:
        /**
         * @brief Draw the Open Project dialog when the editor asked for it.
         *
         * @param ec           Editor context; supplies the state flag and what the
         *                     switch re-roots.
         * @param scriptModule The module to swap for the new project's own.
         */
        void drawDialog(EditorContext& ec, ScriptModule& scriptModule);

        /**
         * @brief Switch the editor to the project rooted at @p projectRoot.
         *
         * Re-roots the paths, reloads the asset library, swaps the gameplay
         * module and boots the project's scene - in that order.
         *
         * @param ec           Editor context to re-root.
         * @param scriptModule Module to reload from the new project.
         * @param projectRoot Directory holding the project's project.json.
         * @return True when the directory was a project and the switch ran.
         */
        bool open(EditorContext& ec, ScriptModule& scriptModule, const std::string& projectRoot);

        /**
         * @brief Record the project the editor started in as most-recent.
         *
         * The project opened from the command line never went through open(),
         * so without this the one you are actually in is the only one missing
         * from its own recent list.
         *
         * @param ec Editor context holding the MRU.
         */
        void noteCurrentProject(EditorContext& ec);

    private:
        /// Push @p projectRoot to the front of the MRU, de-duplicated and capped.
        void pushRecent(EditorContext& ec, const std::string& projectRoot);

    private:
        char m_pathBuffer[512] = {};
};

} // namespace Engine
