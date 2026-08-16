#pragma once

#include <algorithm>

#include <imgui.h>

#include "ui/editor_style.h"

namespace Engine {

/**
 * @brief Shared modal-dialog scaffold: one look and one keyboard contract for
 * every editor dialog.
 *
 * Escape cancels and Enter confirms, the latter only when no text field is
 * capturing input; a field that should commit on Enter uses
 * ImGuiInputTextFlags_EnterReturnsTrue and the caller treats that as confirm.
 *
 * Usage:
 *   if (beginDialog("Rename Asset", m_renameOpen)) {
 *       // ...content...
 *       switch (dialogButtons(m_renameOpen, "Rename", nameValid)) { ... }
 *       endDialog();
 *   }
 */

enum class DialogResult {
    None,     ///< Nothing chosen this frame; the dialog stays open.
    Confirm,  ///< The confirm (rightmost, accent) button or Enter.
    Alt,      ///< The optional third action ("Don't Save").
    Cancel,   ///< The cancel button or Escape.
};

/**
 * @brief Open (when @p wantOpen) and begin the centered modal.
 *
 * @param title    The modal's ImGui title (also its popup id).
 * @param wantOpen Dialog-visible intent; cleared here when the popup was
 *                 dismissed by any path that skipped dialogButtons.
 * @return Whether the modal is open; content + dialogButtons + endDialog run
 *         only when true.
 */
inline bool beginDialog(const char* title, bool& wantOpen) {
    if (wantOpen && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const bool open = ImGui::BeginPopupModal(title, nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    if (!open) wantOpen = false;
    return open;
}

/**
 * @brief The standard dialog button row: [alt] [cancel] [Confirm], right-aligned.
 *
 * Closes the popup and clears @p wantOpen when any result fires. Escape always
 * cancels; Enter confirms while @p confirmEnabled and no text field has the
 * keyboard.
 *
 * @param wantOpen       The same intent flag beginDialog received.
 * @param confirmLabel   Rightmost (accent, default) action.
 * @param confirmEnabled Gates both the button and the Enter shortcut.
 * @param cancelLabel    The dismiss action (default "Cancel").
 * @param altLabel       Optional third action drawn left of cancel.
 * @return What fired this frame (None while the dialog stays open).
 */
inline DialogResult dialogButtons(bool& wantOpen, const char* confirmLabel,
                                  bool confirmEnabled = true,
                                  const char* cancelLabel = "Cancel",
                                  const char* altLabel = nullptr) {
    const ImGuiStyle& style = ImGui::GetStyle();

    // One width for the whole row: the widest label, floored at 96 design px.
    float bw = EditorStyle::px(96.0f);
    auto fit = [&](const char* label) {
        if (label) bw = std::max(bw, ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f);
    };
    fit(confirmLabel); fit(cancelLabel); fit(altLabel);

    const int   n     = altLabel ? 3 : 2;
    const float total = n * bw + (n - 1) * style.ItemSpacing.x;

    ImGui::Spacing();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                  ImGui::GetContentRegionMax().x - total));

    DialogResult r = DialogResult::None;
    if (altLabel) {
        if (ImGui::Button(altLabel, ImVec2(bw, 0))) r = DialogResult::Alt;
        ImGui::SameLine();
    }
    if (ImGui::Button(cancelLabel, ImVec2(bw, 0))) r = DialogResult::Cancel;
    ImGui::SameLine();
    ImGui::BeginDisabled(!confirmEnabled);
    ImGui::PushStyleColor(ImGuiCol_Button,        EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  EditorStyle::ACCENT);
    if (ImGui::Button(confirmLabel, ImVec2(bw, 0))) r = DialogResult::Confirm;
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    if (r == DialogResult::None) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            r = DialogResult::Cancel;
        } else if (confirmEnabled && !ImGui::GetIO().WantTextInput
                   && (ImGui::IsKeyPressed(ImGuiKey_Enter)
                       || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            r = DialogResult::Confirm;
        }
    }

    if (r != DialogResult::None) {
        wantOpen = false;
        ImGui::CloseCurrentPopup();
    }
    return r;
}

/**
 * @brief End the modal begun by a true-returning beginDialog.
 */
inline void endDialog() { ImGui::EndPopup(); }

} // namespace Engine
