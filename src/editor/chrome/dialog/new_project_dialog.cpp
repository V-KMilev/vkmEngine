#include "chrome/dialog/new_project_dialog.h"

#include <cstdio>

#include <imgui.h>

#include "editor_state.h"
#include "io/project/roots.h"
#include "ui/editor_dialogs.h"
#include "ui/style/editor_style.h"

namespace Vkm::Engine {

namespace {

/// What New Project copies unless it is asked for another project.
std::filesystem::path defaultTemplate() { return Roots::engineRoot() / "templates" / "default"; }

} // namespace

void NewProjectDialog::draw(EditorState& state) {
    if (const std::optional<NewProject> asked = state.requests.take<NewProject>()) {
        m_open   = true;
        m_source = asked->from.empty() ? defaultTemplate() : std::filesystem::path(asked->from);
        // An example's copy starts under the example's name.
        if (!asked->from.empty()) {
            const std::string name = m_source.filename().string();
            std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", name.c_str());
        }
        if (m_parentBuffer[0] == '\0') {
            const std::string parent = Roots::projectRoot().parent_path().string();
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
        // vkm makes it, as `vkm new` would from a terminal; the Console's Build view shows how it went.
        state.requests.ask(VkmRun{{"new", dest.string(), "-t", m_source.filename().string()}, dest.string()});
        m_nameBuffer[0] = '\0';
    }
    endDialog();
}

} // namespace Vkm::Engine
