#define VKM_LOG_CATEGORY "EDITOR"

#include "session/project_controller.h"

#include <filesystem>
#include <system_error>

#include "logger.h"

#include "core/clock.h"
#include "ecs/scene.h"
#include "editor_context.h"

#include "editor_settings.h"
#include "editor_state.h"
#include "io/asset/asset_library.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "session/scene_io_controller.h"
#include "project_boot.h"
#include "system/audio/audio_system.h"
#include "system/script/script_module.h"

namespace fs = std::filesystem;

namespace Vkm::Engine::ProjectController {

bool open(
    EditorContext& ec,
    ScriptModule& scriptModule,
    SceneIOController& sceneIO,
    const std::string& projectRoot,
    OpenKind kind
) {
    const fs::path root = findProjectRoot(projectRoot);
    if (root.empty()) {
        // Only a failure when somebody asked for this one. At startup the
        // editor looks beside itself and usually finds nothing, which is not a
        // fault.
        if (kind == OpenKind::Requested) {
            LOG_ERROR("'%s' is not a project (no project.json in it or above it)", projectRoot.c_str());
            ec.state.pushToast(ToastKind::Error, "Not a project: " + projectRoot);
        } else {
            LOG_INFO("No project beside the editor - opening the start screen");
        }
        return false;
    }
    // Whether a project is open, not which way in this is: the start screen's
    // first open has nothing to write or tear down either, and writing it would
    // put editor_settings.json in the engine root.
    const bool replacing = ec.state.projectOpen;

    // editor_settings.json is per project, so the open one's tuning has to be
    // written while its root is still current, or it lands in the project being
    // opened - which at startup would be a default layout over the real one.
    if (replacing) {
        sceneIO.rememberView(ec.state);
        EditorSettings::save(ec.state, ec.frame.render);
    }

    ProjectPaths::setProjectRoot(root);

    // Reset before the read, not after: loadProject leaves untouched fields
    // alone, so opening a project with no maxPlayers would otherwise inherit the
    // one left open.
    Project& project = ec.state.project;
    project = Project{};
    loadProject(root, project);

    // The look the project ships, then the editor's view defaults, then - in
    // EditorSettings::load below - this editor's persisted view state; the
    // shipped and view sets are disjoint, so neither overwrites the other.
    ec.frame.render = project.render;
    EditorSettings::applyViewDefaults(ec.frame.render);

    // Emptied before the assets it references go away, through the teardown a
    // New Scene runs: behaviors get onDestroy while the old module still holds
    // their code, and the scene-scoped editor state belongs to the project left.
    if (replacing) sceneIO.beginSceneReplace(ec.frame, ec.state);

    AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
    EditorSettings::load(ec.state, ec.frame.render);

    // Behaviors from any previous module went with the scene above, and its
    // sounds go here, so nothing of the last project plays on into this one.
    ec.audioSystem.stopEverything();
    const fs::path modulePath = gameplayModulePath();
    std::error_code moduleEc;
    bool moduleFailed = false;
    if (fs::exists(modulePath, moduleEc)) {
        // A failed load is otherwise silent: the project opens looking fine
        // and every behavior in the scene loads as text that never runs.
        moduleFailed = !scriptModule.load(modulePath, &ec.frame.events);
    } else {
        // Unload rather than leave the last project's module in place: it would
        // still answer buildScene below and generate the previous project's
        // world inside this one, and its behavior types would stay registered.
        // Expected rather than forgotten, so the first build is picked up.
        scriptModule.expect(modulePath, ec.frame.events);
        LOG_WARNING("Project '%s' has no gameplay module", project.name.c_str());
    }

    // The editor never hosts or joins, but it is where a game is authored, so an
    // author has to be able to read what it puts on the wire.
    scriptModule.setupNetwork(ec.frame.net);

    const SceneBootResult boot = bootProjectWorld(
        project,
        scriptModule,
        ec.frame.scene,
        ec.frame.resources,
        ec.frame.clock,
        ec.frame.net
    );

    sceneIO.adoptPath(ec.frame.scene, ec.state, boot.path);

    ec.state.sceneDirty = false;

    pushRecentPath(ec.state.recentProjects, root.string());
    // One slot, so the last push is the only one anyone sees.
    if (moduleFailed) {
        const char* withoutModule =
            " without its gameplay module - see the Errors panel. Behaviors are kept but do not "
            "run; fix the build and use File > Reload Scripts.";
        ec.state.pushToast(ToastKind::Error, "Opened " + project.name + withoutModule);
    } else {
        ec.state.pushToast(ToastKind::Info, "Opened " + project.name);
    }
    LOG_INFO("Opened project '%s' at '%s'", project.name.c_str(), root.string().c_str());
    ec.state.projectOpen = true;
    return true;
}

} // namespace Vkm::Engine::ProjectController
