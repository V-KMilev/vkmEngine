#pragma once

#include <cstdint>
#include <string>

namespace Vkm::Engine {

struct EditorContext;
class ScriptModule;
class SceneIOController;

/**
 * @brief Opening a project, which is the one ordered sequence for doing so.
 *
 * A project is the unit the editor edits, so opening one means rooting
 * everything scoped to it: the paths, the asset library, the editor settings,
 * the gameplay module and the scene. That ordering is the whole content of this
 * class - each step depends on the one before, and doing them out of order
 * leaves the editor showing one project's scene with another's assets.
 *
 * It holds no state, deliberately: choosing which project to open belongs to
 * EditorActions::OpenProjectDialog, and the request it makes is answered by the
 * unsaved-changes guard before it ever reaches here.
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
         * @brief Which of the two ways into open() this is.
         *
         * Startup is the project the host was launched on, opened before the
         * first frame: there is no outgoing project whose settings must be
         * written and no scene to tear down, and doing either would write a
         * default editor layout over the settings file about to be read. Switch
         * is File > Open Project, where both of those steps are what leaves the
         * project being closed intact.
         */
        enum class OpenKind : uint8_t { Startup, Switch };

        /**
         * @brief Root the editor in the project at @p projectRoot.
         *
         * The editor's only project-open sequence, for both ways in. Writes the
         * outgoing project's settings, re-roots the paths, tears the old scene
         * down, loads the project's asset library and editor settings, swaps the
         * gameplay module and boots the project's scene - in that order, since
         * each step composes paths or reads code the one before it put in place.
         *
         * Failure is reported and survivable: a directory with no project.json
         * is refused before anything is torn down, and a project whose entry
         * scene will not load still opens on the default scene, because the
         * editor is where that gets fixed.
         *
         * @param ec           Editor context to root.
         * @param scriptModule Module to load from the project's bin/.
         * @param sceneIO      Owns the scene-replace teardown and adopts the
         *                     path the project booted, so Save writes it back.
         * @param projectRoot  The project directory, or any path inside it.
         * @param kind         Which way in this is; see OpenKind.
         * @return True when the directory was a project and the open ran.
         */
        bool open(EditorContext& ec, ScriptModule& scriptModule, SceneIOController& sceneIO,
                  const std::string& projectRoot, OpenKind kind);
};

} // namespace Vkm::Engine
