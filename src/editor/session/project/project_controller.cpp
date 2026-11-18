#define VKM_LOG_CATEGORY "EDITOR"

#include "session/project/project_controller.h"

#include <filesystem>
#include <system_error>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "ecs/scene.h"
#include "editor_context.h"

#include "editor_settings.h"
#include "editor_state.h"
#include "io/asset/asset_library.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "font_baker.h"
#include "vkm_platform/window/window_manager.h"
#include "session/scene_session.h"
#include "system/loading/loading_system.h"
#include "core/project_boot.h"
#include "system/audio/audio_system.h"
#include "system/script/module/script_module.h"

namespace fs = std::filesystem;

namespace Vkm::Engine::ProjectController {

bool open(EditorContext& ec, ScriptModule& scriptModule, const std::string& projectRoot, OpenKind kind) {
    const fs::path root = findProjectRoot(projectRoot);
    if (root.empty()) {
        // At startup the editor looks beside itself and usually finds nothing: not a fault.
        if (kind == OpenKind::Requested) {
            LOG_ERROR("'%s' is not a project (no project.json in it or above it)", projectRoot.c_str());
            ec.state.toasts.push(ToastKind::Error, "Not a project: " + projectRoot);
        } else {
            LOG_INFO("No project beside the editor - opening the start screen");
        }
        return false;
    }
    // Whether one is open, not the way in: with none, writing would put editor_settings.json in the
    // engine root.
    const bool replacing = ec.state.projectOpen;

    // Written while its root is current, or it lands in the project being opened.
    if (replacing) {
        ec.session.rememberView(ec.state);
        EditorSettings::save(ec.state, ec.frame.chrome);
    }

    // Reset before the read: loadProject leaves absent fields alone, which would keep the last project's.
    Project& project = ec.state.project;
    project = Project{};
    loadProject(root, project);
    ec.state.projectSaved = project;

    // Emptied while the old module still holds its behaviors' code, for their onDestroy.
    if (replacing) ec.session.beginSceneReplace(ec.frame, ec.state);

    // Re-rooted after the teardown, whose crash copy is the project left's to discard.
    Roots::setProjectRoot(root);

    // After the teardown, whose session end puts back the old project's settings. The display is the
    // editor's defaults, then this editor's own (EditorSettings::load below).
    ec.frame.graphics.setProject(project.render);
    ec.state.previewTier.reset();  // the last project's
    EditorSettings::applyViewDefaults(ec.frame.chrome);
    bakeProjectFonts(ec.frame.resources, project.fonts, uiFontPixels(ec.frame.window.displayHeight()));
    ec.loadingSystem.setScreen(project.loading);

    AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
    EditorSettings::load(ec.state, ec.frame.chrome);

    // So nothing of the last project plays on; its behaviors went with the scene above.
    ec.audioSystem.stopEverything();
    const fs::path modulePath = Roots::gameplayModule();
    std::error_code moduleEc;
    bool moduleFailed = false;
    if (fs::exists(modulePath, moduleEc)) {
        // Otherwise silent: every behavior in the scene would load as text that never runs.
        moduleFailed = !scriptModule.load(modulePath, &ec.frame.events);
    } else {
        // Unloaded, or the last module's behavior types stay registered; expected, so the first
        // build is picked up.
        scriptModule.expect(modulePath, ec.frame.events);
        LOG_INFO("Project '%s' has no gameplay module yet", project.name.c_str());
    }

    // The editor never hosts or joins, but an author reads what the game puts on the wire.
    scriptModule.setupNetwork(ec.frame.net);

    // The project opens behind the loading screen, as a game's world does.
    ec.frame.chrome.coverNextWorld = true;
    const SceneBootResult boot = bootProjectWorld(
        project,
        ec.frame.scene,
        ec.frame.resources,
        ec.frame.clock,
        ec.frame.input,
        ec.frame.net
    );

    ec.session.adoptPath(ec.frame.scene, ec.state, boot.path);

    ec.state.sceneDirty = false;

    pushRecentPath(ec.state.recentProjects, root.string());
    if (moduleFailed) {
        const char* withoutModule =
            " without its gameplay module: behaviors are kept but do not run; fix the build and use "
            "File > Reload Scripts";
        ec.state.toasts.push(ToastKind::Error, "Opened " + project.name + withoutModule, ConsoleView::Errors);
    } else {
        ec.state.toasts.push(ToastKind::Info, "Opened " + project.name);
    }
    LOG_INFO("Opened project '%s' at '%s'", project.name.c_str(), root.string().c_str());
    ec.state.projectOpen = true;
    return true;
}

} // namespace Vkm::Engine::ProjectController
