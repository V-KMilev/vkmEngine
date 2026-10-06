#define VKM_LOG_CATEGORY "EDITOR"

#include "chrome/new_project_dialog.h"

#include <cstdio>
#include <system_error>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "logger.h"

#include "editor_state.h"
#include "io/json_file.h"
#include "io/project_paths.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

namespace {

/// What New Project copies unless it is asked for another project.
std::filesystem::path defaultTemplate() { return ProjectPaths::engineRoot() / "templates" / "default"; }

} // namespace

bool NewProjectDialog::create(
    const std::filesystem::path& source,
    const std::filesystem::path& dest,
    std::string& error
) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        error = "That directory already exists and is not empty";
        return false;
    }

    if (!fs::is_regular_file(source / "project.json", ec)) {
        error = "No project to copy at " + source.string();
        return false;
    }

    // The source is a runnable project, so what running it wrote is skipped, by name.
    // Must match `vkm new` (tools/vkmcli/project.py); docs_tests holds the two to one set.
    static const char* const GENERATED[] = {
        "build",
        "bin",
        "dist",
        "cooked",
        "logs",
        "__pycache__",
        "editor_settings.json"
    };
    const auto isGenerated = [&](const std::string& name) {
        for (const char* generated : GENERATED) {
            if (name == generated) return true;
        }
        return false;
    };

    fs::create_directories(dest, ec);
    for (fs::recursive_directory_iterator it(source, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        const bool directory = it->is_directory(entryEc);
        if (isGenerated(it->path().filename().string())) {
            if (directory) it.disable_recursion_pending();
            continue;
        }
        const fs::path relative = fs::relative(it->path(), source, entryEc);
        if (entryEc) continue;

        if (directory) fs::create_directories(dest / relative, entryEc);
        else           fs::copy_file(
            it->path(),
            dest / relative,
            fs::copy_options::overwrite_existing,
            entryEc
        );
        if (entryEc) {
            error = "Could not copy the project: " + entryEc.message();
            return false;
        }
    }
    if (ec) {
        error = "Could not read the project to copy: " + ec.message();
        return false;
    }

    // Stamped with this engine: the host compares the string against its own, and
    // the module's build refuses another minor release (vkm_check_engine_version).
    const fs::path projectFile = dest / "project.json";
    nlohmann::json doc;
    if (!detail::readJsonFile(projectFile, doc, "project")) {
        error = "The copied project.json could not be read";
        return false;
    }
    doc["name"]          = dest.filename().string();
    doc["engineVersion"] = APP_VERSION;
    if (!detail::writeJsonFile(projectFile, doc, "project")) {
        error = "Could not write project.json";
        return false;
    }
    return true;
}

void NewProjectDialog::draw(EditorState& state) {
    if (state.requestNewProject) {
        state.requestNewProject = false;
        m_open  = true;
        m_source = state.newProjectTemplate.empty()
            ? defaultTemplate()
            : std::filesystem::path(state.newProjectTemplate);
        // An example's copy starts under the example's name.
        if (!state.newProjectTemplate.empty()) {
            const std::string name = m_source.filename().string();
            std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", name.c_str());
        }
        state.newProjectTemplate.clear();
        if (m_parentBuffer[0] == '\0') {
            const std::string parent = ProjectPaths::projectRoot().parent_path().string();
            std::snprintf(m_parentBuffer, sizeof(m_parentBuffer), "%s", parent.c_str());
        }
    }
    if (!beginDialog("New Project", m_open)) return;

    ImGui::TextDisabled("A project is a directory: its scenes, its assets, and the code that plays them.");
    if (m_source != defaultTemplate()) {
        ImGui::TextDisabled("A copy of %s, yours to change.", m_source.filename().string().c_str());
    }
    ImGui::Spacing();

    ImGui::TextDisabled("Name");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    const bool entered = ImGui::InputText(
        "##NewProjectName",
        m_nameBuffer,
        sizeof(m_nameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );

    ImGui::TextDisabled("In");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    ImGui::InputText("##NewProjectParent", m_parentBuffer, sizeof(m_parentBuffer));

    const std::string name   = m_nameBuffer;
    const std::string parent = m_parentBuffer;
    const bool named = !name.empty() && name.find_first_of("/\\") == std::string::npos;
    const std::filesystem::path dest = named && !parent.empty()
        ? std::filesystem::path(parent) / name : std::filesystem::path{};

    if (!dest.empty()) ImGui::TextDisabled("%s", dest.string().c_str());

    const DialogResult r = dialogButtons(m_open, "Create", named && !parent.empty(), entered);
    if (r == DialogResult::Confirm) {
        std::string error;
        if (create(m_source, dest, error)) {
            state.requestSceneAction(EditorState::SceneAction::OpenProject, dest.string());
            m_nameBuffer[0] = '\0';
        } else {
            // A toast: dialogButtons has already closed the popup, so a message
            // drawn inside it would never show.
            state.pushToast(ToastKind::Error, error);
            LOG_ERROR("New Project: %s", error.c_str());
        }
    }
    endDialog();
}

} // namespace Vkm::Engine
