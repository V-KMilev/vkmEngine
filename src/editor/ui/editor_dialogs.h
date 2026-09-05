#pragma once

#include <algorithm>
#include <cfloat>

#include <imgui.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

enum class DialogResult {
    None,     ///< Nothing chosen this frame; the dialog stays open.
    Confirm,  ///< The confirm (rightmost, accent) button or Enter.
    Alt,      ///< The optional third action ("Don't Save").
    Cancel,   ///< The cancel button or Escape.
};

/**
 * @brief Open (when @p wantOpen) and begin the centered modal - the first of the
 * three calls that make up the shared dialog scaffold.
 *
 * beginDialog / dialogButtons / endDialog give every editor dialog one look and
 * one keyboard contract, Escape cancelling and Enter confirming:
 *
 *   if (beginDialog("Rename Asset", m_renameOpen)) {
 *       // ...content...
 *       switch (dialogButtons(m_renameOpen, "Rename", nameValid)) { ... }
 *       endDialog();
 *   }
 *
 * A dialog sizes itself to its content unless given a size - right for a
 * question, wrong for a list the author resizes and keeps resized.
 *
 * @param title    The modal's ImGui title (also its popup id).
 * @param wantOpen Dialog-visible intent; cleared here when the popup was
 *                 dismissed by any path that skipped dialogButtons.
 * @param initialSize Size on the first open, in framebuffer pixels; the author's
 *                    own size wins afterwards. Zero fits the content instead.
 * @param minimumSize Smallest the author may drag it to. Read only when
 *                    @p initialSize is given.
 * @return Whether the modal is open; content + dialogButtons + endDialog run
 *         only when true.
 */
inline bool beginDialog(const char* title, bool& wantOpen,
                        ImVec2 initialSize = ImVec2(0.0f, 0.0f),
                        ImVec2 minimumSize = ImVec2(0.0f, 0.0f)) {
    if (wantOpen && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (initialSize.x > 0.0f) {
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(minimumSize, ImVec2(FLT_MAX, FLT_MAX));
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }

    const bool open = ImGui::BeginPopupModal(title, nullptr, flags);
    if (!open) wantOpen = false;
    return open;
}

/**
 * @brief The three-way dialog button row: [alt] [Cancel] [Confirm], right-aligned.
 *
 * Closes the popup and clears @p wantOpen when any result fires. Escape always
 * cancels; Enter confirms while @p confirmEnabled and either no text field has
 * the keyboard or @p fieldCommitted says one just committed - ImGui holds
 * WantTextInput while a field is active, which would otherwise swallow Enter in
 * the dialogs that most need it.
 *
 * The alt label has no default and stands ahead of both flags, so nothing but a
 * label can land in its slot; the old order, with a flag ahead of the label, is
 * refused below rather than silently drawing two buttons.
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
 * @brief Refuse a call that puts a flag ahead of the labels.
 *
 * dialogButtons(want, "Save", true, "Don't Save") has no overload taking a bool
 * third, so it would otherwise resolve to the two-button one, bind the label to
 * fieldCommitted and draw a dialog missing its third button with Enter
 * confirming unprompted. An exact match on const char* outranks that bool
 * conversion, so the call lands here and fails.
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

/**
 * @brief The editor's one rename dialog: a name field and the two buttons.
 *
 * A rename is typing, so the field takes the keyboard the frame the dialog
 * appears with the old name selected, and Enter answers it - reaching back for
 * the mouse is the whole gesture spent twice. One copy, shared by the Asset
 * Browser and the Material tab, because two drift.
 *
 * The caller owns the buffer and does the renaming; this owns the look and the
 * keyboard contract. @p open is cleared by any path that closes the dialog, so
 * a caller with a target to forget can watch it.
 *
 * @param title Modal title, and its popup id: what is being renamed.
 * @param open Dialog-visible intent, set by the caller to raise it.
 * @param buf Edit buffer, seeded by the caller with the current name.
 * @param bufSize Size of @p buf.
 * @return true on the frame Rename is confirmed with a non-empty name.
 */
inline bool renameDialog(const char* title, bool& open, char* buf, size_t bufSize) {
    if (!beginDialog(title, open)) return false;

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(EditorStyle::px(280.0f));
    const bool committed = ImGui::InputText("##rnbuf", buf, bufSize,
                                            ImGuiInputTextFlags_EnterReturnsTrue
                                          | ImGuiInputTextFlags_AutoSelectAll);
    const DialogResult result = dialogButtons(open, "Rename", buf[0] != '\0', committed);
    endDialog();
    return result == DialogResult::Confirm;
}

} // namespace Vkm::Engine
