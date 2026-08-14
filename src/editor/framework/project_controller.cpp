#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/project_controller.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <imgui.h>

#include "logger.h"

#include "core/engine.h"
#include "ecs/scene.h"
#include "framework/editor_context.h"
#include "framework/editor_settings.h"
#include "framework/editor_state.h"
#include "io/asset/asset_library.h"
#include "system/render/render_system.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "generator/default_scene.h"
#include "platform/library/dynamic_library.h"
#include "system/script/script_module.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"

namespace fs = std::filesystem;

namespace Engine {

bool ProjectController::open(EditorContext& ec, ScriptModule& scriptModule,
                             const std::string& projectRoot) {
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

    // 3. Empty the scene before the assets it references go away.
    ec.state.deselect();
    ec.frame.scene.clear();

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
        LOG_WARNING("Project '%s' has no gameplay module", project.name.c_str());
    }

    // 6. Whatever the project says it starts as, by the same rule and in the
    //    same order both binaries boot with: an authored scene, else one its
    //    module generates, else the default scene.
    bool booted = false;
    if (!project.entryScene.empty()) {
        const fs::path scene = root / project.entryScene;
        booted = SceneSerializer::load(ec.frame.scene, ec.frame.resources, scene.string());
        if (!booted) LOG_ERROR("Entry scene '%s' failed to load", scene.string().c_str());
    } else {
        booted = scriptModule.buildScene(ec.frame.scene);
    }

    if (!booted) buildDefaultScene(ec.frame.scene, ec.frame.resources);

    ec.frame.window.setTitle(project.name + " - vkmEngine");
    ec.state.sceneDirty = false;

    pushRecent(ec, root.string());
    ec.state.pushToast(EditorState::ToastKind::Info, "Opened " + project.name);
    LOG_INFO("Opened project '%s' at '%s'", project.name.c_str(), root.string().c_str());
    return true;
}

void ProjectController::noteCurrentProject(EditorContext& ec) {
    pushRecent(ec, ProjectPaths::projectRoot().string());
}

void ProjectController::pushRecent(EditorContext& ec, const std::string& projectRoot) {
    std::vector<std::string>& recents = ec.state.recentProjects;
    recents.erase(std::remove(recents.begin(), recents.end(), projectRoot), recents.end());
    recents.insert(recents.begin(), projectRoot);
    if (recents.size() > EditorState::MAX_RECENT_SCENES) {
        recents.resize(EditorState::MAX_RECENT_SCENES);
    }
}

void ProjectController::drawDialog(EditorContext& ec, ScriptModule& scriptModule) {
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

    DialogResult r = dialogButtons(ec.state.showOpenProject, "Open", typedIsProject);
    if (entered && typedIsProject && r == DialogResult::None) {
        r = DialogResult::Confirm;
        ec.state.showOpenProject = false;
        ImGui::CloseCurrentPopup();
    }
    if (r == DialogResult::Confirm) chosen = typed;

    if (!chosen.empty()) {
        ec.state.showOpenProject = false;
        ImGui::CloseCurrentPopup();
    }

    endDialog();

    // Outside the popup scope: open() rebuilds the scene, and doing that with
    // an ImGui window still on the stack is asking for trouble.
    if (!chosen.empty()) {
        open(ec, scriptModule, chosen);
        m_pathBuffer[0] = '\0';
    }
}

} // namespace Engine
