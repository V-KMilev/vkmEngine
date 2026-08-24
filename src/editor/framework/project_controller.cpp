#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/project_controller.h"

#include <filesystem>
#include <system_error>

#include "logger.h"

#include "ecs/scene.h"
#include "framework/editor_context.h"
#include "framework/editor_settings.h"
#include "framework/editor_state.h"
#include "io/asset/asset_library.h"
#include "system/render/render_system.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "framework/scene_io_controller.h"
#include "project_boot.h"
#include "platform/library/dynamic_library.h"
#include "system/script/script_module.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

bool ProjectController::open(EditorContext& ec, ScriptModule& scriptModule,
                             SceneIOController& sceneIO, const std::string& projectRoot,
                             OpenKind kind) {
    // findProjectRoot accepts the directory or anything inside it, so a path
    // typed with a trailing file name still resolves.
    const fs::path root = findProjectRoot(projectRoot);
    if (root.empty()) {
        LOG_ERROR("'%s' is not a project (no project.json above it)", projectRoot.c_str());
        ec.state.pushToast(EditorState::ToastKind::Error, "Not a project: " + projectRoot);
        return false;
    }
    const bool replacing = (kind == OpenKind::Switch);

    // editor_settings.json is per project, so the open one's tuning has to be
    // written while its root is still current, or it lands in the project being
    // opened - which at startup would be a default layout over the real one.
    if (replacing) EditorSettings::save(ec.state, ec.renderSystem.getSettings());

    // Re-root, so every path composed below resolves in this project.
    ProjectPaths::setProjectRoot(root);

    Project project;
    loadProject(root, project);

    // Empty the scene before the assets it references go away, through the same
    // teardown a New Scene runs: behaviors get onDestroy while the old module
    // still holds their code, and the undo stack, material previews and
    // saved-scene path all belong to the project being left.
    if (replacing) sceneIO.beginSceneReplace(ec.frame, ec.state);

    AssetLibrary::get().load();
    EditorSettings::load(ec.state, ec.renderSystem.getSettings());

    // The project's own code. Behaviors from any previous module went with the
    // scene above, so nothing is left pointing at code this unloads.
    const fs::path modulePath =
        ProjectPaths::projectBin() / DynamicLibrary::platformName("game");
    std::error_code moduleEc;
    if (fs::exists(modulePath, moduleEc)) {
        scriptModule.load(modulePath.string());
    } else {
        // Unload rather than leave the last project's module in place: it would
        // still answer buildScene below and generate the previous project's
        // world inside this one, and its behavior types would stay registered.
        scriptModule.unload();
        LOG_WARNING("Project '%s' has no gameplay module", project.name.c_str());
    }

    // Whatever the project says it starts as, by the rule and in the order both
    // binaries boot with: an authored scene, else one its module generates, else
    // the default scene. A project whose entry scene will not load still opens,
    // and says so through the engine's error sink.
    const SceneBootResult boot =
        bootProjectScene(project, scriptModule, ec.frame.scene, ec.frame.resources);

    // The scene came in without passing through the scene controller, so the
    // path is handed over for it to be the file this project is editing. Empty
    // for a module-built world and for the default scene standing in, which is
    // what leaves those two asking for a name on the first save.
    sceneIO.adoptPath(ec.state, boot.path);

    // The window title is composed once per frame from the editor state (see
    // EditorSystem); setting it here as well would be overwritten next frame.
    ec.state.projectName = project.name;
    ec.state.sceneDirty  = false;

    pushRecentPath(ec.state.recentProjects, root.string());
    ec.state.pushToast(EditorState::ToastKind::Info, "Opened " + project.name);
    LOG_INFO("Opened project '%s' at '%s'", project.name.c_str(), root.string().c_str());
    return true;
}

} // namespace Vkm::Engine
