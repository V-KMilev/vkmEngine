#pragma once

#include <algorithm>

#include <imgui.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

/**
 * @brief Shared modal-dialog scaffold: one look and one keyboard contract for
 * every editor dialog.
 *
 * Escape cancels and Enter confirms. ImGui holds WantTextInput while a field
 * is active, which would otherwise swallow Enter in exactly the dialogs that
 * most need it, so a field wanting Enter to confirm passes its
 * ImGuiInputTextFlags_EnterReturnsTrue result as dialogButtons' fieldCommitted
 * argument - the caller decides whether a commit means confirm, the scaffold
 * still owns closing the popup.
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
 * @brief The three-way dialog button row: [alt] [Cancel] [Confirm], right-aligned.
 *
 * Closes the popup and clears @p wantOpen when any result fires. Escape always
 * cancels; Enter confirms while @p confirmEnabled and either no text field has
 * the keyboard or @p fieldCommitted says one just committed.
 *
 * The alt label has no default and stands ahead of both flags, so nothing but a
 * label can land in its slot and it cannot be reached past one; the old order,
 * with a flag ahead of the label, is refused below rather than silently drawing
 * two buttons. A surplus label in a flag's slot does still convert to true, so
 * what the compiler settles is the row, not the whole argument list.
 *
 * @param wantOpen       The same intent flag beginDialog received.
 * @param confirmLabel   Rightmost (accent, default) action.
 * @param altLabel       Third action drawn left of Cancel, or nullptr for none.
 * @param confirmEnabled Gates the button and both Enter paths.
 * @param fieldCommitted A text field in this dialog returned true from
 *                       EnterReturnsTrue this frame.
 * @return What fired this frame (None while the dialog stays open).
 */
inline DialogResult dialogButtons(bool& wantOpen, const char* confirmLabel,
                                  const char* altLabel,
                                  bool confirmEnabled = true,
                                  bool fieldCommitted = false) {
    const ImGuiStyle& style = ImGui::GetStyle();

    // One width for the whole row: the widest label, floored at 96 design px.
    float bw = EditorStyle::px(96.0f);
    auto fit = [&](const char* label) {
        if (label) bw = std::max(bw, ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f);
    };
    fit(confirmLabel); fit("Cancel"); fit(altLabel);

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
    if (ImGui::Button("Cancel", ImVec2(bw, 0))) r = DialogResult::Cancel;
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
        } else if (confirmEnabled
                   && (fieldCommitted
                       || (!ImGui::GetIO().WantTextInput
                           && (ImGui::IsKeyPressed(ImGuiKey_Enter)
                               || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))))) {
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
 * @brief The two-button dialog button row: [Cancel] [Confirm], right-aligned.
 *
 * The common case, and the reason the alt action is a separate overload rather
 * than a trailing default: reaching a defaulted label past a defaulted flag is
 * what lets a mistyped call bind a label to the flag and lose a button.
 *
 * @param wantOpen       The same intent flag beginDialog received.
 * @param confirmLabel   Rightmost (accent, default) action.
 * @param confirmEnabled Gates the button and both Enter paths.
 * @param fieldCommitted A text field in this dialog returned true from
 *                       EnterReturnsTrue this frame.
 * @return What fired this frame (None while the dialog stays open).
 */
inline DialogResult dialogButtons(bool& wantOpen, const char* confirmLabel,
                                  bool confirmEnabled = true,
                                  bool fieldCommitted = false) {
    return dialogButtons(wantOpen, confirmLabel, nullptr, confirmEnabled, fieldCommitted);
}

/**
 * @brief Refuse the old parameter order, which put a flag ahead of the labels.
 *
 * dialogButtons(want, "Save", true, "Don't Save") is the shape a call written
 * against that order keeps: no overload takes a bool third, so it would resolve
 * to the two-button one, bind the label to fieldCommitted and draw a dialog
 * missing its third button with Enter confirming unprompted. An exact match on
 * const char* outranks that bool conversion, so the call lands here and fails.
 *
 * @param wantOpen       The same intent flag beginDialog received.
 * @param confirmLabel   Rightmost (accent, default) action.
 * @param confirmEnabled Gates the button and both Enter paths.
 * @param altLabel       Third action, which belongs before the flags.
 */
inline DialogResult dialogButtons(bool& wantOpen, const char* confirmLabel,
                                  bool confirmEnabled, const char* altLabel) = delete;

/**
 * @brief End the modal begun by a true-returning beginDialog.
 */
inline void endDialog() { ImGui::EndPopup(); }

} // namespace Vkm::Engine
