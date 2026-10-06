#include "chrome/open_project_dialog.h"

#include <string>

#include <imgui.h>

#include "editor_state.h"
#include "io/project.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"

namespace Vkm::Engine {

void OpenProjectDialog::draw(EditorState& state) {
    if (state.requestOpenProject) {
        state.requestOpenProject = false;
        m_open = true;
    }
    if (!beginDialog("Open Project", m_open)) return;

    ImGui::TextDisabled("A project is a directory with a project.json in it.");
    ImGui::Spacing();

    ImGui::TextDisabled("Path");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    const bool entered = ImGui::InputText(
        "##ProjectPath",
        m_pathBuffer,
        sizeof(m_pathBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );

    const std::string typed = m_pathBuffer;
    const bool typedIsProject = !typed.empty() && !findProjectRoot(typed).empty();
    if (!typed.empty() && !typedIsProject) {
        ImGui::TextColored(EditorStyle::WARNING, "No project.json here");
    }

    const DialogResult r = dialogButtons(m_open, "Open", typedIsProject, entered);
    endDialog();

    if (r == DialogResult::Confirm) {
        state.requestSceneAction(EditorState::SceneAction::OpenProject, typed);
        m_pathBuffer[0] = '\0';
    }
}

} // namespace Vkm::Engine
