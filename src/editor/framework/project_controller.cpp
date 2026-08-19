#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/project_controller.h"

#include <cstring>
#include <filesystem>
#include <system_error>

#include <imgui.h>

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
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

bool ProjectController::open(EditorContext& ec, ScriptModule& scriptModule,
                             SceneIOController& sceneIO, const std::string& projectRoot) {
    // findProjectRoot accepts the directory or anything inside it, so a path
    // typed with a trailing file name still resolves.
    const fs::path root = findProjectRoot(projectRoot);
    if (root.empty()) {
        LOG_ERROR("'%s' is not a project (no project.json above it)", projectRoot.c_str());
        ec.state.pushToast(EditorState::ToastKind::Error, "Not a project: " + projectRoot);
        return false;
    }

    // Order matters, and each step depends on the one before it.
    // 1. Hand the settings over before the root moves. editor_settings.json is
    //    per project, so the open one's tuning has to be written while its root
    //    is still current - otherwise it would land in the project being opened.
    EditorSettings::save(ec.state, ec.renderSystem.getSettings());

    // 2. Re-root, so every path composed below resolves in the new project.
    ProjectPaths::setProjectRoot(root);

    Project project;
    loadProject(root, project);

    // 3. Empty the scene before the assets it references go away, through the
    //    same teardown a New Scene runs: behaviors get onDestroy while the old
    //    module still holds their code, and the undo stack, material previews
    //    and saved-scene path all belong to the project being left.
    sceneIO.beginSceneReplace(ec.frame, ec.state);


    // 4. The new project's asset database, and its own editor settings.
    AssetLibrary::get().load();
    EditorSettings::load(ec.state, ec.renderSystem.getSettings());

    // 5. The new project's code. Behaviors from the old module are gone with
    //    the scene, so nothing is left pointing at code this unloads.
    const fs::path modulePath =
        ProjectPaths::projectBin() / DynamicLibrary::platformName("game");
    std::error_code ec2;
    if (fs::exists(modulePath, ec2)) {
        scriptModule.load(modulePath.string());
    } else {
        // Unload rather than leave the last project's module in place: it would
        // still answer buildScene below and generate the previous project's
        // world inside this one, and its behavior types would stay registered.
        scriptModule.unload();
        LOG_WARNING("Project '%s' has no gameplay module", project.name.c_str());
    }

    // 6. Whatever the project says it starts as, by the same rule and in the
    //    same order both binaries boot with: an authored scene, else one its
    //    module generates, else the default scene.
    bootProjectScene(project, scriptModule, ec.frame.scene, ec.frame.resources);

    // The window title is composed once per frame from the editor state (see
    // EditorSystem); setting it here as well would be overwritten next frame.
    ec.state.projectName = project.name;
    ec.state.sceneDirty  = false;

    pushRecentPath(ec.state.recentProjects, root.string());
    ec.state.pushToast(EditorState::ToastKind::Info, "Opened " + project.name);
    LOG_INFO("Opened project '%s' at '%s'", project.name.c_str(), root.string().c_str());
    return true;
}

void ProjectController::noteCurrentProject(EditorContext& ec) {
    pushRecentPath(ec.state.recentProjects, ProjectPaths::projectRoot().string());
}

void ProjectController::drawDialog(EditorContext& ec, ScriptModule& scriptModule,
                                   SceneIOController& sceneIO) {
    if (!beginDialog("Open Project", ec.state.showOpenProject)) return;

    ImGui::TextDisabled("A project is a directory with a project.json in it.");
    ImGui::Spacing();

    // Recents first: switching between a few projects is the common case, and
    // typing a path for it every time would be the wrong default.
    std::string chosen;
    if (!ec.state.recentProjects.empty()) {
        ImGui::TextDisabled("Recent");
        for (const std::string& path : ec.state.recentProjects) {
            ImGui::PushID(path.c_str());
            const std::string label = fs::path(path).filename().string();
            if (ImGui::Selectable(label.empty() ? path.c_str() : label.c_str())) {
                chosen = path;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::TextDisabled("Path");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    const bool entered = ImGui::InputText("##ProjectPath", m_pathBuffer, sizeof(m_pathBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

    const std::string typed = m_pathBuffer;
    const bool typedIsProject = !typed.empty() && !findProjectRoot(typed).empty();
    if (!typed.empty() && !typedIsProject) {
        ImGui::TextColored(EditorStyle::WARNING, "No project.json here");
    }

    const DialogResult r = dialogButtons(ec.state.showOpenProject, "Open",
                                         typedIsProject, entered);
    if (r == DialogResult::Confirm) chosen = typed;

    if (!chosen.empty()) {
        ec.state.showOpenProject = false;
        ImGui::CloseCurrentPopup();
    }

    endDialog();

    // Outside the popup scope: open() rebuilds the scene, and doing that with
    // an ImGui window still on the stack is asking for trouble.
    if (!chosen.empty()) {
        // Park it rather than open here: the deferred path is the one that asks
        // about unsaved changes first, and both ways in should ask.
        ec.state.pendingProjectOpen = chosen;
        m_pathBuffer[0] = '\0';
    }
}

} // namespace Vkm::Engine
